"""
Incremental taxonomy builder for A2X.

Supports adding new services to and removing existing services from
a pre-built taxonomy tree without full rebuild.
"""

import logging
import threading
from typing import Dict, List, Optional
from concurrent.futures import ThreadPoolExecutor, as_completed

from a2x_registry.common.llm_client import LLMClient, parse_json_response

logger = logging.getLogger(__name__)


SELECT_DOMAINS_TEMPLATE = """Which of the following top-level functional domains are relevant for this service?

SERVICE:
ID: {service_id}
Name: {service_name}
Description: {service_description}

DOMAINS:
{domains_text}

RULES:
- Select ALL domains where a user browsing that domain would benefit from discovering this service
- Most services belong to 1 domain; some cross-cutting services may belong to 2-3
- Be selective: only include domains where the service is genuinely useful

Return ONLY the numbers separated by commas (e.g. "1,3"), or "NONE" if no domain fits."""


PLACE_IN_CATEGORY_TEMPLATE = """Place this service into the best leaf sub-category within the target domain.

SERVICE:
ID: {service_id}
Name: {service_name}
Description: {service_description}

TARGET DOMAIN: {domain_name} ({domain_desc})

AVAILABLE SUB-CATEGORIES:
{subcategories_text}

Select the single best-fit sub-category. Output JSON:
```json
{{
  "target_category_id": "...",
  "confidence": 85
}}
```

If no sub-category fits (confidence < 40), set target_category_id to "{domain_id}" (place at domain root)."""


class IncrementalBuilder:
    """Add/remove services from an existing A2X taxonomy."""

    def __init__(
        self,
        taxonomy: dict,
        class_data: dict,
        services_index: Dict[str, dict],
        llm_client: LLMClient,
        workers: int = 20,
    ):
        self.taxonomy = taxonomy
        self.class_data = class_data
        self.services_index = services_index
        self.llm = llm_client
        self.workers = workers
        self._lock = threading.Lock()

    def remove_service(self, service_id: str) -> bool:
        """Remove a service from all taxonomy nodes and the services index.

        Returns True if the service was found and removed.
        """
        found = False
        with self._lock:
            for cat_data in self.taxonomy['categories'].values():
                services = cat_data.get('services', [])
                if service_id in services:
                    services.remove(service_id)
                    found = True
            self.services_index.pop(service_id, None)

        if found:
            logger.info(f"Removed service {service_id}")
        else:
            logger.warning(f"Service {service_id} not found in taxonomy")
        return found

    def add_service(self, service: dict) -> List[str]:
        """Add a service to the taxonomy by assigning it to appropriate leaf nodes.

        Args:
            service: dict with at least 'id', 'name', 'description'.

        Returns:
            List of category IDs the service was assigned to.
        """
        service_id = service.get('id', service.get('name', ''))
        if not service_id:
            logger.warning("Service has no id or name, skipping")
            return []

        # Store in services index
        with self._lock:
            self.services_index[service_id] = service

        # Get top-level domains
        root_children = self.taxonomy['categories'].get('root', {}).get('children', [])
        if not root_children:
            # Flat taxonomy: place directly under root
            with self._lock:
                root_services = self.taxonomy['categories']['root'].setdefault('services', [])
                if service_id not in root_services:
                    root_services.append(service_id)
                    root_services.sort()
            return ['root']

        domains = {}
        for domain_id in root_children:
            info = self.class_data.get('categories', {}).get(domain_id, {})
            domains[domain_id] = {
                'name': info.get('name', domain_id),
                'description': info.get('description', ''),
            }

        # Phase 1: select relevant domains
        selected_domain_ids = self._select_domains(service, domains)
        if not selected_domain_ids:
            # No domain matched, place under root
            with self._lock:
                root_services = self.taxonomy['categories']['root'].setdefault('services', [])
                if service_id not in root_services:
                    root_services.append(service_id)
                    root_services.sort()
            logger.info(f"Service {service_id} placed under root (no domain matched)")
            return ['root']

        # Phase 2: place in leaf category within each domain
        assigned_categories = self._place_in_domains(service, selected_domain_ids, domains)

        if not assigned_categories:
            # Fallback: place under root
            with self._lock:
                root_services = self.taxonomy['categories']['root'].setdefault('services', [])
                if service_id not in root_services:
                    root_services.append(service_id)
                    root_services.sort()
            return ['root']

        # Write assignments into taxonomy
        with self._lock:
            for cat_id in assigned_categories:
                cat_data = self.taxonomy['categories'].get(cat_id)
                if cat_data is not None:
                    services = cat_data.setdefault('services', [])
                    if service_id not in services:
                        services.append(service_id)
                        services.sort()

        logger.info(f"Service {service_id} assigned to {assigned_categories}")
        return assigned_categories

    def add_services_batch(self, services: List[dict]) -> Dict[str, List[str]]:
        """Add multiple services in parallel.

        Returns:
            Dict mapping service_id to list of assigned category IDs.
        """
        results = {}
        with ThreadPoolExecutor(max_workers=self.workers) as executor:
            futures = {
                executor.submit(self.add_service, svc): svc.get('id', svc.get('name', ''))
                for svc in services
            }
            for future in as_completed(futures):
                svc_id = futures[future]
                try:
                    results[svc_id] = future.result()
                except Exception as e:
                    logger.warning(f"Failed to add service {svc_id}: {e}")
                    results[svc_id] = []
        return results

    # ----- internal -----

    def _select_domains(self, service: dict, domains: Dict[str, dict]) -> List[str]:
        domain_ids = sorted(domains.keys())
        domain_lines = []
        for i, did in enumerate(domain_ids):
            d = domains[did]
            domain_lines.append(f"{i+1}. {d['name']}: {d['description']}")

        prompt = SELECT_DOMAINS_TEMPLATE.format(
            service_id=service.get('id', ''),
            service_name=service.get('name', ''),
            service_description=service.get('description', 'No description'),
            domains_text="\n".join(domain_lines),
        )

        response = self.llm.call(
            messages=[{"role": "user", "content": prompt}],
            temperature=0.0, max_tokens=100,
        )
        if not response.success:
            return []

        selected = self._parse_selection(response.content, len(domain_ids))
        return [domain_ids[idx] for idx in selected]

    def _place_in_domains(
        self, service: dict, domain_ids: List[str], domains: Dict[str, dict]
    ) -> List[str]:
        assigned = []

        if len(domain_ids) == 1:
            cat_id = self._place_in_domain(service, domain_ids[0], domains[domain_ids[0]])
            if cat_id:
                assigned.append(cat_id)
        else:
            with ThreadPoolExecutor(max_workers=min(self.workers, len(domain_ids))) as executor:
                futures = {
                    executor.submit(self._place_in_domain, service, did, domains[did]): did
                    for did in domain_ids
                }
                for future in as_completed(futures):
                    try:
                        cat_id = future.result()
                        if cat_id:
                            assigned.append(cat_id)
                    except Exception as e:
                        logger.warning(f"Placement error: {e}")

        return assigned

    def _place_in_domain(self, service: dict, domain_id: str, domain_info: dict) -> Optional[str]:
        leaves = self._get_leaves_under(domain_id)
        if not leaves:
            return domain_id

        leaf_info = {}
        for lid in leaves:
            info = self.class_data.get('categories', {}).get(lid, {})
            leaf_info[lid] = {
                'name': info.get('name', lid),
                'description': info.get('description', ''),
            }

        cat_lines = [
            f"- {cid}: {info['name']} — {info.get('description', '')}"
            for cid, info in sorted(leaf_info.items())
        ]

        prompt = PLACE_IN_CATEGORY_TEMPLATE.format(
            service_id=service.get('id', ''),
            service_name=service.get('name', ''),
            service_description=service.get('description', 'No description'),
            domain_name=domain_info['name'],
            domain_desc=domain_info.get('description', ''),
            subcategories_text="\n".join(cat_lines),
            domain_id=domain_id,
        )

        response = self.llm.call(
            messages=[{"role": "user", "content": prompt}],
            temperature=0.0, max_tokens=300,
        )
        if not response.success:
            return None

        result = parse_json_response(response.content)
        if not result:
            return None

        target = result.get('target_category_id', '')
        confidence = result.get('confidence', 0)
        if confidence < 40 or not target:
            return domain_id

        # Validate target exists in taxonomy
        if target not in self.taxonomy['categories']:
            return domain_id

        return target

    def _get_leaves_under(self, node_id: str) -> List[str]:
        cat_data = self.taxonomy['categories'].get(node_id, {})
        children = cat_data.get('children', [])
        if not children:
            return [node_id]
        leaves = []
        stack = list(children)
        while stack:
            cid = stack.pop()
            c_data = self.taxonomy['categories'].get(cid, {})
            c_children = c_data.get('children', [])
            if not c_children:
                leaves.append(cid)
            else:
                stack.extend(c_children)
        return leaves

    @staticmethod
    def _parse_selection(response: str, max_index: int) -> List[int]:
        response = response.strip().upper()
        if response == "NONE" or not response:
            return []
        indices = []
        parts = response.replace(",", " ").split()
        for part in parts:
            try:
                num = int(part.strip())
                if 1 <= num <= max_index:
                    indices.append(num - 1)
            except ValueError:
                continue
        return indices
