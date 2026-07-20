"""
A2X Hierarchical Taxonomy Search

Orchestrates two-phase search:
  Phase 1 (Navigator): LLM-guided recursive category navigation
  Phase 2 (Selector):  Service dedup, grouping, and LLM selection
"""

import json
import logging
import queue
import threading
from pathlib import Path
from typing import List, Dict, Optional, Callable, Generator, Union

from a2x_registry.common.llm_client import LLMClient
from a2x_registry.a2x.search.models import SearchStats, NavigationStep
from a2x_registry.a2x.search.navigator import CategoryNavigator
from a2x_registry.a2x.search.selector import ServiceSelector

logger = logging.getLogger(__name__)

VALID_MODES = ("get_all", "get_one", "get_important")


class A2XSearch:
    """
    Two-phase hierarchical taxonomy search.

    Phase 1: Recursively traverse taxonomy, LLM selects relevant categories.
    Phase 2: Collect services from terminal nodes, deduplicate, merge, LLM selects services.

    Modes:
        get_all       — select all potentially relevant (high recall)
        get_one       — select single best match (high precision), falls back to get_important if empty
        get_important — select only clearly needed services (balanced)
    """

    def __init__(
        self,
        taxonomy_path: str = None,
        class_path: str = None,
        service_path: str = None,
        max_workers: int = 20,
        parallel: bool = True,
        mode: str = "get_all",
    ):
        if mode not in VALID_MODES:
            raise ValueError(f"Invalid mode: {mode}. Must be one of {VALID_MODES}.")
        self.mode = mode

        # Default to ToolRet_clean inside the resolved A2X_REGISTRY_HOME
        from a2x_registry.common.paths import dataset_dir
        _default_ds = dataset_dir("ToolRet_clean")

        taxonomy_path = taxonomy_path or str(_default_ds / "taxonomy" / "taxonomy.json")
        class_path = class_path or str(_default_ds / "taxonomy" / "class.json")
        service_path = service_path or str(_default_ds / "service.json")

        with open(taxonomy_path, 'r', encoding='utf-8') as f:
            taxonomy_data = json.load(f)
        self.categories = taxonomy_data.get("categories", {})
        self.root_id = taxonomy_data.get("root", "root")
        self.taxonomy = taxonomy_data

        with open(class_path, 'r', encoding='utf-8') as f:
            class_data = json.load(f)
        classes = class_data.get("categories", {})

        with open(service_path, 'r', encoding='utf-8') as f:
            services_list = json.load(f)
        services = {s['id']: s for s in services_list}

        # Shared LLM client
        self.llm = LLMClient(pool_maxsize=200)
        self.max_workers = max_workers
        self.parallel = parallel

        # Taxonomy tree helpers
        self._parent_map = self._build_parent_map()

        # Compose navigator and selector
        self._navigator = CategoryNavigator(
            llm=self.llm,
            categories=self.categories,
            classes=classes,
            mode=mode,
            max_workers=max_workers,
            parallel=parallel,
        )
        self._selector = ServiceSelector(
            llm=self.llm,
            services=services,
            mode=mode,
            max_workers=max_workers,
            parallel=parallel,
            get_ancestors=self._get_ancestors,
            lca_depth=self._lca_depth,
        )

    # =========================================================================
    # Taxonomy tree helpers
    # =========================================================================

    def _build_parent_map(self) -> Dict[str, str]:
        parent_map = {}
        for cat_id, cat_data in self.categories.items():
            for child_id in cat_data.get('children', []):
                parent_map[child_id] = cat_id
        return parent_map

    def _get_ancestors(self, node_id: str) -> List[str]:
        path = []
        current = node_id
        while current:
            path.append(current)
            current = self._parent_map.get(current)
        path.reverse()
        return path

    @staticmethod
    def _lca_depth(path_a: List[str], path_b: List[str]) -> int:
        depth = -1
        for a, b in zip(path_a, path_b):
            if a != b:
                break
            depth += 1
        return depth

    # =========================================================================
    # Search entry points
    # =========================================================================

    def search(
        self,
        query: str,
        stream: bool = False,
    ) -> Union[tuple, Generator[Dict, None, None]]:
        """
        Search for services matching the query.

        Args:
            query: Natural language query
            stream: If True, returns generator yielding navigation steps then result

        Returns:
            stream=False: (List[SearchResult], SearchStats)
            stream=True:  Generator yielding dicts
        """
        if not stream:
            return self._search_internal(query)
        return self._search_stream(query)

    def _search_internal(
        self,
        query: str,
        step_callback: Optional[Callable[[NavigationStep], None]] = None,
    ) -> tuple:
        """Core search pipeline: navigate → dedup → merge → select."""
        stats = SearchStats()

        # Phase 1: Category navigation
        terminal_nodes = self._navigator.navigate(
            query, self.root_id, stats, step_callback=step_callback,
        )

        # Signal Phase 2 start (for streaming UI)
        if step_callback is not None:
            try:
                step_callback(NavigationStep(
                    parent_id="__phase2__", selected=[], pruned=[],
                ))
            except Exception:
                pass

        # Phase 2: Dedup + merge + select
        terminal_nodes = self._selector.deduplicate(terminal_nodes)
        groups = self._selector.merge_small_groups(terminal_nodes)
        results = self._selector.select_services(query, groups, stats)

        # Final dedup
        seen_ids = set()
        unique_results = []
        for result in results:
            if result.id not in seen_ids:
                seen_ids.add(result.id)
                unique_results.append(result)

        # get_one fallback: re-run as get_important and take top 1
        if self.mode == "get_one" and not unique_results:
            logger.info("get_one returned empty, falling back to get_important")
            if step_callback is not None:
                try:
                    step_callback(NavigationStep(
                        parent_id="__fallback__", selected=[], pruned=[],
                    ))
                except Exception:
                    pass
            original_mode = self.mode
            self._set_mode("get_important")
            try:
                fb_results, fb_stats = self._search_internal(query, step_callback=step_callback)
                stats.update(llm_calls=fb_stats.llm_calls, tokens=fb_stats.total_tokens)
                if fb_results:
                    unique_results = [fb_results[0]]
            finally:
                self._set_mode(original_mode)

        return unique_results, stats

    def _set_mode(self, mode: str):
        """Switch mode on self and sub-components."""
        self.mode = mode
        self._navigator.mode = mode
        self._selector.mode = mode

    def _search_stream(self, query: str) -> Generator[Dict, None, None]:
        """Generator that yields NavigationSteps in real-time, then final result."""
        step_queue: queue.Queue = queue.Queue()
        result_holder: list = []

        def on_step(nav_step: NavigationStep):
            step_queue.put({
                "type": "step",
                "parent_id": nav_step.parent_id,
                "selected": nav_step.selected,
                "pruned": nav_step.pruned,
            })

        def worker():
            results, stats = self._search_internal(query, step_callback=on_step)
            result_holder.append((results, stats))
            step_queue.put(None)

        t = threading.Thread(target=worker, daemon=True)
        t.start()

        while True:
            msg = step_queue.get()
            if msg is None:
                break
            yield msg

        results, stats = result_holder[0]
        yield {
            "type": "result",
            "results": [
                {"id": r.id, "name": r.name, "description": r.description}
                for r in results
            ],
            "stats": {
                "llm_calls": stats.llm_calls,
                "total_tokens": stats.total_tokens,
                "visited_categories": len(stats.visited_categories),
                "pruned_categories": len(stats.pruned_categories),
            },
        }
