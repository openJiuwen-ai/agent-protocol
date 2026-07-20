"""Phase 2+3: Service deduplication, grouping, and LLM-based selection."""

import logging
from concurrent.futures import ThreadPoolExecutor, as_completed
from typing import List, Dict, Callable

from a2x_registry.a2x.search.models import (
    SearchResult, SearchStats, TerminalNode, ServiceGroup,
)
from a2x_registry.a2x.search.prompts import build_service_prompt, parse_selection

logger = logging.getLogger(__name__)


class ServiceSelector:
    """Deduplicates, groups, and selects services from terminal nodes."""

    MIN_GROUP_SIZE = 30

    def __init__(
        self,
        llm,
        services: Dict,
        mode: str,
        max_workers: int = 20,
        parallel: bool = True,
        get_ancestors: Callable[[str], List[str]] = None,
        lca_depth: Callable[[List[str], List[str]], int] = None,
    ):
        self.llm = llm
        self.services = services
        self.mode = mode
        self.max_workers = max_workers
        self.parallel = parallel
        self._get_ancestors = get_ancestors
        self._lca_depth = lca_depth

    def _get_service_info(self, service_id: str) -> Dict:
        if service_id in self.services:
            return self.services[service_id]
        return {"id": service_id, "name": service_id, "description": ""}

    def deduplicate(self, terminal_nodes: List[TerminalNode]) -> List[TerminalNode]:
        """Remove duplicate services across terminal nodes (first-come-first-served)."""
        seen = set()
        result = []
        for node in terminal_nodes:
            unique = [sid for sid in node.service_ids if sid not in seen]
            seen.update(unique)
            if unique:
                result.append(TerminalNode(node.category_id, unique))
        return result

    def merge_small_groups(self, terminal_nodes: List[TerminalNode]) -> List[ServiceGroup]:
        """Merge groups with < MIN_GROUP_SIZE services by LCA proximity."""
        if not terminal_nodes:
            return []

        groups: List[ServiceGroup] = []
        group_paths: List[Dict[str, List[str]]] = []

        for node in terminal_nodes:
            groups.append(ServiceGroup(
                leaf_ids={node.category_id},
                service_ids=list(node.service_ids),
            ))
            group_paths.append(
                {node.category_id: self._get_ancestors(node.category_id)}
            )

        while len(groups) > 1:
            small_idx = -1
            small_size = float('inf')
            for i, g in enumerate(groups):
                if len(g.service_ids) < self.MIN_GROUP_SIZE and len(g.service_ids) < small_size:
                    small_size = len(g.service_ids)
                    small_idx = i

            if small_idx == -1:
                break

            best_idx = -1
            best_lca = -1
            paths_small = group_paths[small_idx]

            for j, g in enumerate(groups):
                if j == small_idx:
                    continue
                paths_other = group_paths[j]
                lca_values = [
                    self._lca_depth(pa, pb)
                    for pa in paths_small.values()
                    for pb in paths_other.values()
                ]
                if not lca_values:
                    continue
                max_lca = max(lca_values)
                if max_lca > best_lca:
                    best_lca = max_lca
                    best_idx = j

            if best_idx == -1:
                break

            target = groups[best_idx]
            source = groups[small_idx]
            existing = set(target.service_ids)
            for sid in source.service_ids:
                if sid not in existing:
                    target.service_ids.append(sid)
                    existing.add(sid)
            target.leaf_ids |= source.leaf_ids
            group_paths[best_idx].update(group_paths[small_idx])

            groups.pop(small_idx)
            group_paths.pop(small_idx)

        return groups

    def select_services(
        self,
        query: str,
        groups: List[ServiceGroup],
        stats: SearchStats,
    ) -> List[SearchResult]:
        """Select relevant services from all groups (parallel if enabled)."""
        if not groups:
            return []

        results = []

        if self.parallel and len(groups) > 1:
            with ThreadPoolExecutor(max_workers=min(self.max_workers, len(groups))) as executor:
                futures = {
                    executor.submit(
                        self._select_for_group, query, group, stats
                    ): group
                    for group in groups
                }
                for future in as_completed(futures):
                    try:
                        results.extend(future.result())
                    except Exception as e:
                        logger.error(f"Error in parallel service selection: {e}")
        else:
            for group in groups:
                results.extend(self._select_for_group(query, group, stats))

        return results

    def _select_for_group(
        self,
        query: str,
        group: ServiceGroup,
        stats: SearchStats,
    ) -> List[SearchResult]:
        """Ask LLM to select relevant services from a single group."""
        if not group.service_ids:
            return []

        prompt = build_service_prompt(
            self.mode, query, group.service_ids, self._get_service_info,
        )
        messages = [{"role": "user", "content": prompt}]

        try:
            response = self.llm.call(messages, temperature=0.0, max_tokens=200)
            stats.update(llm_calls=1, tokens=response.tokens)

            selected_indices = parse_selection(response.content, len(group.service_ids), self.mode)

            results = []
            for idx in selected_indices:
                svc_id = group.service_ids[idx]
                svc = self._get_service_info(svc_id)
                results.append(SearchResult(
                    id=svc_id,
                    name=svc.get('name', ''),
                    description=svc.get('description', ''),
                ))

            return results
        except Exception as e:
            logger.error(f"Error selecting services: {e}")
            return []
