"""Cross-domain multi-parent assignment for hierarchical taxonomy.

After the main hierarchical build, this module identifies services that should
also appear in other top-level domains to improve discoverability.
"""

import logging
from typing import Dict, List
from concurrent.futures import ThreadPoolExecutor, as_completed

from a2x_registry.common.llm_client import LLMClient, parse_json_response
from .progress import progress_bar

logger = logging.getLogger(__name__)


SYSTEM_CROSS_DOMAIN = (
    "You are an expert at API service classification. You identify services that "
    "should be discoverable from multiple functional domains."
)

IDENTIFY_CROSS_DOMAIN_TEMPLATE = """You are reviewing services in the category "{source_category_name}" ({source_category_desc}).

These services currently belong to this category. Some of them may ALSO be relevant to users browsing OTHER top-level domains.

TOP-LEVEL DOMAINS (excluding current):
{other_domains_text}

SERVICES IN THIS CATEGORY:
{services_text}

For each service, decide if it should ALSO appear in one of the other top-level domains listed above.
A service should be cross-listed if a user looking for that type of functionality would reasonably browse the other domain.

RULES:
- Only suggest cross-domain assignments that are genuinely useful for discoverability
- Maximum 1 additional domain per service
- Do NOT suggest cross-listing for services that clearly belong to only one domain
- Be selective: typically 10-30% of services benefit from cross-listing

Output JSON:
```json
{{
  "cross_assignments": [
    {{
      "service_id": "...",
      "target_domain_id": "...",
      "reason": "brief explanation"
    }}
  ]
}}
```

If no services need cross-listing, return: {{"cross_assignments": []}}"""


PLACE_IN_DOMAIN_TEMPLATE = """Place this service into the best leaf sub-category within the target domain.

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
  "service_id": "{service_id}",
  "target_category_id": "...",
  "confidence": 85
}}
```

If no sub-category fits (confidence < 40), set target_category_id to "{domain_id}" (place at domain root)."""


class CrossDomainAssigner:
    """Identifies and assigns cross-domain multi-parent relationships."""

    def __init__(self, llm_client: LLMClient, workers: int = 20):
        self.client = llm_client
        self.workers = workers

    def assign(
        self,
        taxonomy: dict,
        class_data: dict,
        services_index: Dict[str, Dict],
    ) -> Dict[str, List[str]]:
        """Run cross-domain assignment.

        Returns:
            Dict mapping service_id -> list of additional category_ids to add.
        """
        logger.info("CROSS-DOMAIN MULTI-PARENT ASSIGNMENT")

        root_children = taxonomy['categories'].get('root', {}).get('children', [])
        if len(root_children) < 2:
            logger.info("Only 1 top-level domain, skipping cross-domain assignment")
            return {}

        # Build domain info
        domains = {}
        for domain_id in root_children:
            info = class_data['categories'].get(domain_id, {})
            domains[domain_id] = {
                'name': info.get('name', domain_id),
                'description': info.get('description', ''),
            }

        # Phase 1: For each leaf category, identify cross-domain candidates
        candidates = self._phase1_identify(taxonomy, class_data, services_index, domains)
        if not candidates:
            logger.info("No cross-domain candidates identified")
            return {}

        logger.info(f"Phase 1 complete: {len(candidates)} cross-domain candidates")

        # Phase 2: Place each candidate in target domain's best leaf
        additions = self._phase2_place(candidates, taxonomy, class_data, services_index, domains)

        logger.info(f"Phase 2 complete: {len(additions)} services assigned to additional categories")
        return additions

    def _phase1_identify(
        self,
        taxonomy: dict,
        class_data: dict,
        services_index: Dict[str, Dict],
        domains: Dict[str, dict],
    ) -> List[Dict]:
        """Phase 1: Identify services needing cross-domain assignment.

        Process each leaf category that has services, asking LLM which services
        should also appear in other top-level domains.
        """
        # Collect leaf categories grouped by their top-level domain
        leaf_tasks = []
        for domain_id in sorted(domains.keys()):
            leaves = self._get_leaves_under(domain_id, taxonomy)
            for leaf_id in leaves:
                cat_data = taxonomy['categories'].get(leaf_id, {})
                service_ids = cat_data.get('services', [])
                if not service_ids:
                    continue
                leaf_tasks.append((leaf_id, domain_id, service_ids))

        logger.info(f"Phase 1: scanning {len(leaf_tasks)} leaf categories for cross-domain candidates")

        all_candidates = []
        completed = 0

        with ThreadPoolExecutor(max_workers=self.workers) as executor:
            futures = {}
            for leaf_id, domain_id, service_ids in leaf_tasks:
                services = [services_index[sid] for sid in service_ids if sid in services_index]
                if not services:
                    continue
                leaf_info = class_data['categories'].get(leaf_id, {})
                other_domains = {
                    did: dinfo for did, dinfo in domains.items()
                    if did != domain_id
                }
                future = executor.submit(
                    self._identify_for_category,
                    leaf_id, leaf_info, services, other_domains
                )
                futures[future] = leaf_id

            total_futures = len(futures)
            for future in as_completed(futures):
                leaf_id = futures[future]
                completed += 1
                try:
                    result = future.result()
                    if result:
                        all_candidates.extend(result)
                except Exception as e:
                    logger.error(f"Error identifying cross-domain for {leaf_id}: {e}")

                progress_bar(completed, total_futures, suffix=f"{len(all_candidates)} candidates")

        return all_candidates

    def _identify_for_category(
        self,
        leaf_id: str,
        leaf_info: dict,
        services: List[Dict],
        other_domains: Dict[str, dict],
    ) -> List[Dict]:
        """Ask LLM which services in this category should also be in other domains."""
        # Format services
        svc_lines = []
        for svc in services:
            desc = svc.get('description', 'No description')
            if len(desc) > 150:
                desc = desc[:150] + "..."
            svc_lines.append(f"- {svc['id']}: {svc['name']} — {desc}")
        services_text = "\n".join(svc_lines)

        # Format other domains
        domain_lines = []
        for did, dinfo in sorted(other_domains.items()):
            domain_lines.append(f"- {did}: {dinfo['name']} — {dinfo['description']}")
        other_domains_text = "\n".join(domain_lines)

        prompt = IDENTIFY_CROSS_DOMAIN_TEMPLATE.format(
            source_category_name=leaf_info.get('name', leaf_id),
            source_category_desc=leaf_info.get('description', ''),
            other_domains_text=other_domains_text,
            services_text=services_text,
        )

        response = self.client.call(
            messages=[
                {"role": "system", "content": SYSTEM_CROSS_DOMAIN},
                {"role": "user", "content": prompt},
            ],
            temperature=0.1,
            max_tokens=2000,
        )

        if not response.success:
            return []

        result = parse_json_response(response.content)
        if not result:
            return []

        return result.get('cross_assignments', [])

    def _phase2_place(
        self,
        candidates: List[Dict],
        taxonomy: dict,
        class_data: dict,
        services_index: Dict[str, Dict],
        domains: Dict[str, dict],
    ) -> Dict[str, List[str]]:
        """Phase 2: Place each candidate in the best leaf of its target domain."""
        # Group candidates by target domain
        by_domain: Dict[str, List[Dict]] = {}
        for cand in candidates:
            target = cand.get('target_domain_id', '')
            if target and target in domains:
                by_domain.setdefault(target, []).append(cand)

        logger.info(f"Phase 2: placing services in {len(by_domain)} target domains")

        # Pre-compute leaf categories per domain with their info
        domain_leaves: Dict[str, Dict[str, dict]] = {}
        for domain_id in by_domain:
            leaves = self._get_leaves_under(domain_id, taxonomy)
            leaf_info = {}
            for lid in leaves:
                info = class_data['categories'].get(lid, {})
                leaf_info[lid] = {
                    'name': info.get('name', lid),
                    'description': info.get('description', ''),
                }
            # Also include the domain itself if it has no leaves
            if not leaf_info:
                leaf_info[domain_id] = domains[domain_id]
            domain_leaves[domain_id] = leaf_info

        additions: Dict[str, List[str]] = {}
        completed = 0
        total = sum(len(cands) for cands in by_domain.values())

        with ThreadPoolExecutor(max_workers=self.workers) as executor:
            futures = {}
            for domain_id, cands in by_domain.items():
                leaves = domain_leaves[domain_id]
                domain_info = domains[domain_id]
                for cand in cands:
                    svc_id = cand.get('service_id', '')
                    svc = services_index.get(svc_id)
                    if not svc:
                        continue
                    future = executor.submit(
                        self._place_in_domain,
                        svc, domain_id, domain_info, leaves
                    )
                    futures[future] = svc_id

            for future in as_completed(futures):
                svc_id = futures[future]
                completed += 1
                try:
                    target_cat_id = future.result()
                    if target_cat_id:
                        additions.setdefault(svc_id, []).append(target_cat_id)
                except Exception as e:
                    logger.error(f"Error placing {svc_id}: {e}")

                n_placed = sum(len(v) for v in additions.values())
                progress_bar(completed, total, suffix=f"{n_placed} placed")

        return additions

    def _place_in_domain(
        self,
        service: Dict,
        domain_id: str,
        domain_info: dict,
        leaves: Dict[str, dict],
    ) -> str:
        """Place a service in the best leaf category of the target domain."""
        # Format subcategories
        cat_lines = []
        for cat_id, info in sorted(leaves.items()):
            cat_lines.append(f"- {cat_id}: {info['name']} — {info.get('description', '')}")
        subcategories_text = "\n".join(cat_lines)

        prompt = PLACE_IN_DOMAIN_TEMPLATE.format(
            service_id=service['id'],
            service_name=service['name'],
            service_description=service.get('description', 'No description'),
            domain_name=domain_info['name'],
            domain_desc=domain_info.get('description', ''),
            subcategories_text=subcategories_text,
            domain_id=domain_id,
        )

        response = self.client.call(
            messages=[
                {"role": "system", "content": SYSTEM_CROSS_DOMAIN},
                {"role": "user", "content": prompt},
            ],
            temperature=0.0,
            max_tokens=300,
        )

        if not response.success:
            return ""

        result = parse_json_response(response.content)
        if not result:
            return ""

        target_id = result.get('target_category_id', '')
        confidence = result.get('confidence', 0)

        # Only accept reasonable placements
        if confidence < 40:
            return ""

        return target_id

    @staticmethod
    def _get_leaves_under(node_id: str, taxonomy: dict) -> List[str]:
        """Get all leaf category IDs under a node (including self if leaf)."""
        cat_data = taxonomy['categories'].get(node_id, {})
        children = cat_data.get('children', [])

        if not children:
            return [node_id]

        leaves = []
        stack = list(children)
        while stack:
            cid = stack.pop()
            c_data = taxonomy['categories'].get(cid, {})
            c_children = c_data.get('children', [])
            if not c_children:
                leaves.append(cid)
            else:
                stack.extend(c_children)
        return leaves
