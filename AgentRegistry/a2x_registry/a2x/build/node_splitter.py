"""Per-node classify/refine loop for auto-hierarchical taxonomy building.

Orchestrates the full node splitting process:
1. Design subcategories (via CategoryDesigner)
2. Classify ALL services (parallel, 1 LLM call each)
3. Check convergence, refine if needed (via CategoryDesigner), repeat
4. Handle edge cases (generic, tiny, unclassified)
5. Return NodeSplitResult

Classification rules (generic_ratio based):
- Normal: 1 <= n_cats <= N * generic_ratio (accepted, multi-parent OK)
- Generic: n_cats > N * generic_ratio (too many matches → left at parent)
- Unclassified: n_cats == 0 (no match → left at parent)
- Tiny subcategories: <= delete_threshold services after iterations → deleted
"""

import logging
from typing import Dict, List
from concurrent.futures import ThreadPoolExecutor, as_completed

from a2x_registry.common.llm_client import LLMClient, parse_json_response

logger = logging.getLogger(__name__)
from .config import AutoHierarchicalConfig
from .category_designer import CategoryDesigner
from .progress import progress_bar
from .prompts import (
    ClassificationResult,
    NodeSplitResult,
    format_categories_for_prompt,
    SYSTEM_CLASSIFY_NODE,
    CLASSIFY_SERVICE_IN_NODE_TEMPLATE,
)


class NodeSplitter:
    """Orchestrates the full subdivision of a single taxonomy node.

    Uses CategoryDesigner for initial design and refinement,
    handles classification and edge cases internally.
    """

    def __init__(self, llm_client: LLMClient, config: AutoHierarchicalConfig):
        self.client = llm_client
        self.config = config
        self.designer = CategoryDesigner(llm_client, config)

    def split_node(
        self,
        node_id: str,
        parent_info: dict,
        services: List[Dict],
        is_root: bool = False,
    ) -> NodeSplitResult:
        """Subdivide a node into subcategories.

        Three phases: design → classify/refine loop → finalize.
        """
        max_cats = self.config.get_max_categories()

        # Phase 1: Design initial subcategories
        logger.info("Designing subcategories for %s (%d services, max_cats=%d)...",
                    node_id, len(services), max_cats)
        subcategories = self.designer.design_categories(
            services,
            node_info=parent_info if not is_root else None,
            is_root=is_root,
            max_categories=max_cats,
        )
        sub_lines = [f"  {sub_id}: {info['name']}" for sub_id, info in sorted(subcategories.items())]
        logger.info("Designed %d subcategories:\n%s", len(subcategories), "\n".join(sub_lines))

        # Phase 2: Classify/refine iteration
        subcategories, assignments, iteration = self._classify_refine_loop(
            node_id, parent_info, services, subcategories, is_root,
        )

        # Phase 3: Finalize (handle generic/unclassified, delete tiny subcategories)
        unclassified_ids = self._finalize_assignments(assignments, subcategories)

        converged = iteration < self.config.max_refine_iterations
        logger.info("Node %s complete: %d subcategories, %d unclassified",
                    node_id, len(subcategories), len(unclassified_ids))

        return NodeSplitResult(
            node_id=node_id,
            subcategories=subcategories,
            assignments=assignments,
            unclassified_service_ids=unclassified_ids,
            iterations_used=iteration,
            converged=converged,
        )

    def _classify_refine_loop(
        self,
        node_id: str,
        parent_info: dict,
        services: List[Dict],
        subcategories: Dict[str, dict],
        is_root: bool,
    ) -> tuple:
        """Run classify → check → refine loop until convergence.

        Returns:
            (subcategories, assignments, iteration) after convergence or max iterations.
        """
        max_refine = self.config.max_refine_iterations
        assignments = {}

        iteration = 0
        for iteration in range(1, max_refine + 1):
            logger.info("--- Iteration %d/%d ---", iteration, max_refine)

            # Classify all services
            logger.info("Classifying %d services...", len(services))
            assignments = self._classify_all(services, subcategories, parent_info)

            # Compute and log stats
            stats = self._compute_stats(assignments, subcategories)
            logger.info("Results: %d normal, %d generic, %d unclassified",
                        stats['n_normal'], stats['n_generic'], stats['n_unclassified'])
            self._log_distribution(subcategories, stats)

            # Check convergence
            if self._should_terminate(stats, iteration, max_refine):
                logger.info("Node %s converged at iteration %d", node_id, iteration)
                break

            # Refine subcategories
            if iteration < max_refine:
                logger.info("Refining subcategories...")
                subcategories = self.designer.refine_categories(
                    parent_info, subcategories, assignments, services,
                    stats, is_root=is_root,
                )
                ref_lines = [f"  {sub_id}: {info['name']}" for sub_id, info in sorted(subcategories.items())]
                logger.info("After refinement: %d subcategories\n%s", len(subcategories), "\n".join(ref_lines))

        return subcategories, assignments, iteration

    def _finalize_assignments(
        self,
        assignments: Dict[str, dict],
        subcategories: Dict[str, dict],
    ) -> List[str]:
        """Handle generic/unclassified services and delete tiny subcategories.

        Returns:
            List of unclassified service IDs (stay at parent node).
        """
        # Mark generic and unclassified services
        n_subcats = len(subcategories)
        generic_threshold = max(1, int(n_subcats * self.config.generic_ratio))
        unclassified_ids = []

        for svc_id, r in assignments.items():
            n_cats = len(r.get('category_ids', []))
            if n_cats == 0:
                unclassified_ids.append(svc_id)
            elif n_cats > generic_threshold:
                r['category_ids'] = []
                unclassified_ids.append(svc_id)

        # Delete tiny subcategories (≤ delete_threshold services)
        final_stats = self._compute_stats(assignments, subcategories)
        tiny_to_delete = [
            sub_id for sub_id, count in final_stats['cat_counts'].items()
            if count <= self.config.delete_threshold and sub_id in subcategories
        ]

        if tiny_to_delete:
            del_lines = [f"  DELETE: {sub_id} ({subcategories[sub_id]['name']}): "
                         f"{final_stats['cat_counts'].get(sub_id, 0)} services"
                         for sub_id in sorted(tiny_to_delete)]
            logger.info("Deleting %d tiny subcategories (<=%d services):\n%s",
                        len(tiny_to_delete), self.config.delete_threshold, "\n".join(del_lines))

            for sub_id in sorted(tiny_to_delete):
                tiny_svc_ids = [
                    svc_id for svc_id, r in assignments.items()
                    if sub_id in r.get('category_ids', [])
                ]
                for svc_id in tiny_svc_ids:
                    r = assignments[svc_id]
                    cat_ids = [cid for cid in r.get('category_ids', [])
                               if cid not in tiny_to_delete]
                    r['category_ids'] = cat_ids
                    if not cat_ids and svc_id not in unclassified_ids:
                        unclassified_ids.append(svc_id)
                del subcategories[sub_id]

        return unclassified_ids

    def _log_distribution(self, subcategories: Dict[str, dict], stats: dict):
        """Log per-subcategory service distribution."""
        dist_lines = []
        for sub_id, info in sorted(subcategories.items()):
            count = stats['cat_counts'].get(sub_id, 0)
            tiny_mark = " [TINY]" if count <= self.config.min_leaf_size else ""
            dist_lines.append(f"  {sub_id} ({info['name']}): {count} services{tiny_mark}")
        logger.info("Per-subcategory distribution:\n%s", "\n".join(dist_lines))

    # =========================================================================
    # Classification
    # =========================================================================

    def _classify_all(
        self,
        services: List[Dict],
        subcategories: Dict[str, dict],
        parent_info: dict,
    ) -> Dict[str, dict]:
        """Classify all services into subcategories (1/call, parallel)."""
        subcategories_text = format_categories_for_prompt(subcategories)
        valid_ids = set(subcategories.keys())
        results = {}
        total = len(services)
        completed = 0

        def classify_one(svc: Dict) -> ClassificationResult:
            return self._classify_single(svc, subcategories_text, valid_ids, parent_info)

        with ThreadPoolExecutor(max_workers=self.config.workers) as executor:
            futures = {
                executor.submit(classify_one, svc): svc['id']
                for svc in services
            }

            for future in as_completed(futures):
                svc_id = futures[future]
                try:
                    result = future.result()
                    results[svc_id] = {
                        'category_ids': result.category_ids,
                        'reasoning': result.reasoning,
                    }
                except Exception as e:
                    results[svc_id] = {
                        'category_ids': [],
                        'reasoning': f'Error: {e}',
                    }

                completed += 1
                if completed % max(1, total // 100) == 0 or completed == total:
                    n_ok = sum(1 for r in results.values() if r['category_ids'])
                    progress_bar(completed, total, suffix=f"{n_ok} assigned")

        return results

    def _classify_single(
        self,
        service: Dict,
        subcategories_text: str,
        valid_ids: set,
        parent_info: dict,
    ) -> ClassificationResult:
        """Classify one service with retry logic."""
        prompt = CLASSIFY_SERVICE_IN_NODE_TEMPLATE.format(
            parent_name=parent_info.get('name', 'Unknown'),
            parent_description=parent_info.get('description', ''),
            subcategories_text=subcategories_text,
            service_description=service.get('description', 'No description'),
        )

        for attempt in range(1 + self.config.classification_retries):
            response = self.client.call(
                messages=[
                    {"role": "system", "content": SYSTEM_CLASSIFY_NODE},
                    {"role": "user", "content": prompt},
                ],
                temperature=self.config.temperature_classify,
                max_tokens=self.config.max_tokens_classify,
            )

            if not response.success:
                if attempt < self.config.classification_retries:
                    continue
                return ClassificationResult(
                    service_id=service['id'],
                    success=False,
                    error=f"LLM call failed: {response.error}",
                )

            result = parse_json_response(response.content)
            if result:
                cat_ids = result.get('category_ids', [])
                if isinstance(cat_ids, str):
                    cat_ids = [cat_ids]
                cat_ids = [cid for cid in cat_ids if cid in valid_ids]

                return ClassificationResult(
                    service_id=service['id'],
                    category_ids=cat_ids,
                    reasoning=result.get('reasoning', ''),
                )

            # Parse failed, retry
            if attempt < self.config.classification_retries:
                prompt = (
                    "Your previous response was not valid JSON. "
                    "Please output ONLY a JSON object, nothing else.\n\n"
                    + prompt
                )

        return ClassificationResult(
            service_id=service['id'],
            success=False,
            error="Failed to parse response after retries",
        )

    # =========================================================================
    # Helpers
    # =========================================================================

    def _compute_stats(self, assignments: Dict[str, dict], subcategories: Dict[str, dict]) -> dict:
        """Compute classification statistics using generic_ratio.

        Returns dict with:
            n_normal: 1 <= n_cats <= threshold (accepted)
            n_generic: n_cats > threshold (too many matches)
            n_unclassified: n_cats == 0
            cat_counts: {cat_id: service_count}
        """
        n_subcats = len(subcategories)
        threshold = max(1, int(n_subcats * self.config.generic_ratio))

        n_normal = 0
        n_generic = 0
        n_unclassified = 0
        cat_counts = {cat_id: 0 for cat_id in subcategories}

        for svc_id, result in assignments.items():
            cat_ids = result.get('category_ids', [])
            n_cats = len(cat_ids)

            if n_cats == 0:
                n_unclassified += 1
            elif n_cats > threshold:
                n_generic += 1
            else:
                n_normal += 1

            for cat_id in cat_ids:
                if cat_id in cat_counts:
                    cat_counts[cat_id] += 1

        return {
            'n_normal': n_normal,
            'n_generic': n_generic,
            'n_unclassified': n_unclassified,
            'cat_counts': cat_counts,
        }

    @staticmethod
    def _should_terminate(stats: dict, iteration: int, max_refine: int) -> bool:
        """Check if node splitting is complete.

        Terminate when:
        1. Max iterations reached
        2. No generic, no unclassified, no tiny subcategories
        """
        if iteration >= max_refine:
            return True
        if stats['n_generic'] == 0 and stats['n_unclassified'] == 0:
            return True
        return False
