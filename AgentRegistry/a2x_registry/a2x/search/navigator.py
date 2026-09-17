"""Phase 1: LLM-guided recursive category navigation."""

import logging
from concurrent.futures import ThreadPoolExecutor, as_completed
from typing import List, Dict, Optional, Callable

from a2x_registry.a2x.search.models import (
    SearchStats, NavigationStep, TerminalNode,
)
from a2x_registry.a2x.search.prompts import build_category_prompt, parse_selection

logger = logging.getLogger(__name__)


class CategoryNavigator:
    """Recursively navigates taxonomy tree, selecting relevant categories via LLM."""

    def __init__(
        self,
        llm,
        categories: Dict,
        classes: Dict,
        mode: str,
        max_workers: int = 20,
        parallel: bool = True,
    ):
        self.llm = llm
        self.categories = categories
        self.classes = classes
        self.mode = mode
        self.max_workers = max_workers
        self.parallel = parallel

    def _get_category_info(self, category_id: str) -> Dict:
        if category_id in self.classes:
            return self.classes[category_id]
        return {"name": category_id, "description": ""}

    def navigate(
        self,
        query: str,
        category_id: str,
        stats: SearchStats,
        path: str = "",
        depth: int = 0,
        step_callback: Optional[Callable[[NavigationStep], None]] = None,
    ) -> List[TerminalNode]:
        """Recursively navigate taxonomy, return terminal nodes with services."""
        stats.update(visited_ids=[category_id])

        cat_info = self._get_category_info(category_id)
        cat_name = cat_info.get('name', category_id)
        current_path = f"{path}/{cat_name}" if path else cat_name

        cat_data = self.categories.get(category_id, {})
        direct_services = cat_data.get('services', [])
        children = cat_data.get('children', [])

        terminal_nodes = []

        if children:
            selected_child_ids = self._select_categories(
                query, children, current_path, stats,
                parent_id=category_id, step_callback=step_callback,
            )

            if selected_child_ids:
                child_terminals = self._navigate_children(
                    query, selected_child_ids, stats,
                    current_path, depth=depth + 1,
                    step_callback=step_callback,
                )
                terminal_nodes.extend(child_terminals)

                if direct_services:
                    terminal_nodes.append(TerminalNode(category_id, direct_services))
            else:
                if direct_services:
                    terminal_nodes.append(TerminalNode(category_id, direct_services))
        else:
            if direct_services:
                terminal_nodes.append(TerminalNode(category_id, direct_services))

        return terminal_nodes

    def _navigate_children(
        self,
        query: str,
        child_ids: List[str],
        stats: SearchStats,
        parent_path: str,
        depth: int = 0,
        step_callback: Optional[Callable[[NavigationStep], None]] = None,
    ) -> List[TerminalNode]:
        """Navigate selected children in parallel or sequentially."""
        terminal_nodes = []

        if self.parallel and len(child_ids) > 1:
            with ThreadPoolExecutor(max_workers=min(self.max_workers, len(child_ids))) as executor:
                futures = {
                    executor.submit(
                        self.navigate, query, child_id, stats,
                        parent_path, depth, step_callback,
                    ): child_id
                    for child_id in child_ids
                }
                for future in as_completed(futures):
                    try:
                        terminal_nodes.extend(future.result())
                    except Exception as e:
                        logger.error(f"Error in parallel navigation: {e}")
        else:
            for child_id in child_ids:
                terminal_nodes.extend(
                    self.navigate(query, child_id, stats,
                                  parent_path, depth, step_callback)
                )

        return terminal_nodes

    def _select_categories(
        self,
        query: str,
        child_ids: List[str],
        parent_path: str,
        stats: SearchStats,
        parent_id: str = "",
        step_callback: Optional[Callable[[NavigationStep], None]] = None,
    ) -> List[str]:
        """Ask LLM which child categories are relevant. Returns selected IDs."""
        prompt = build_category_prompt(
            self.mode, query, child_ids, self._get_category_info, parent_path,
        )
        messages = [{"role": "user", "content": prompt}]

        try:
            response = self.llm.call(messages, temperature=0.0, max_tokens=200)
            stats.update(llm_calls=1, tokens=response.tokens)

            selected_indices = parse_selection(response.content, len(child_ids), self.mode)

            selected_ids = []
            pruned_ids = []
            visited_paths = []
            pruned_paths = []
            for i, child_id in enumerate(child_ids):
                child_info = self._get_category_info(child_id)
                child_name = child_info.get('name', child_id)
                child_path = f"{parent_path}/{child_name}"
                if i in selected_indices:
                    selected_ids.append(child_id)
                    visited_paths.append(child_path)
                else:
                    pruned_ids.append(child_id)
                    pruned_paths.append(child_path)
            stats.update(visited=visited_paths, pruned=pruned_paths)

            if step_callback is not None:
                try:
                    step_callback(NavigationStep(
                        parent_id=parent_id,
                        selected=selected_ids,
                        pruned=pruned_ids,
                    ))
                except Exception as cb_err:
                    logger.warning(f"step_callback error: {cb_err}")

            return selected_ids
        except Exception as e:
            logger.error(f"Error selecting categories: {e}")
            return []
