"""Unified BFS recursive taxonomy builder for auto-hierarchical pipeline.

Build interface:
    build(resume="no"):      Full rebuild — delete all intermediate results, build from scratch.
    build(resume="keyword"): Rebuild but reuse cached root keywords (for debugging).
    build(resume="yes"):     Smart resume — if completed with matching config, skip;
                             if partial with matching config, continue from checkpoint;
                             otherwise rebuild.

Algorithm:
1. Load services, initialize empty taxonomy with root containing all services
2. BFS queue starts with oversized leaf nodes (root for fresh build)
3. For each oversized node: NodeSplitter.split_node(is_root=(node_id=='root'))
4. Apply result, queue children > max_service_size
5. CrossDomainAssigner post-processing
6. Save output with build_status="complete"

build_status in taxonomy.json tracks progress:
    "bfs"          — BFS splitting in progress
    "cross_domain" — BFS done, cross-domain assignment pending
    "complete"     — build finished
"""

import hashlib
import json
import logging
import shutil
import threading
import time
from collections import deque
from pathlib import Path
from typing import Dict, List, Optional

from a2x_registry.common.llm_client import LLMClient
from .cross_domain_assigner import CrossDomainAssigner
from .config import AutoHierarchicalConfig
from .node_splitter import NodeSplitter, NodeSplitResult

logger = logging.getLogger(__name__)


class TaxonomyBuilder:
    """Unified recursive taxonomy builder.

    Root and child nodes use the same code path with unified parameters.
    """

    def __init__(self, config: AutoHierarchicalConfig,
                 stop_event: Optional[threading.Event] = None):
        self.config = config
        self.stop_event = stop_event

        # State (populated during build)
        self.taxonomy: dict = {}
        self.class_data: dict = {}
        self.services_index: Dict[str, Dict] = {}
        self.all_assignments: Dict[str, dict] = {}

    def _check_cancelled(self):
        """Raise InterruptedError if cancellation was requested."""
        if self.stop_event and self.stop_event.is_set():
            raise InterruptedError("Build cancelled by user")

    # =========================================================================
    # Public Interface
    # =========================================================================

    def build(self, resume: str = "no"):
        """Execute taxonomy build.

        Args:
            resume: Build mode
                "no"      — Full rebuild (delete all intermediate results including keywords)
                "keyword" — Rebuild but reuse cached root keywords (debug/iteration)
                "yes"     — Smart resume: skip if complete, continue if partial,
                            rebuild if config mismatch or no checkpoint
        """
        if resume not in ("no", "keyword", "yes"):
            raise ValueError(f"Invalid resume mode: {resume!r}. Must be 'no', 'keyword', or 'yes'.")

        start_time = time.time()

        # Phase 0: Decide action based on resume mode
        if resume == "yes":
            action = self._evaluate_resume()
            if action == "skip":
                logger.info("Taxonomy already complete with matching config, skipping build")
                return
            elif action == "resume":
                logger.info("Resuming build from checkpoint")
                self._load_existing_output()
            else:  # "rebuild"
                logger.info("Config mismatch or no checkpoint — full rebuild")
                self._clean_output()
        elif resume == "keyword":
            self._clean_output(preserve_keywords=True)
        else:  # "no"
            self._clean_output()

        # Phase 1: Load services, initialize taxonomy if needed
        self._check_cancelled()
        self._load_services_index()
        if not self.taxonomy:
            self._init_taxonomy()

        # Phase 2: BFS splitting
        self._check_cancelled()
        client = LLMClient()
        splitter = NodeSplitter(client, self.config)

        if self.taxonomy.get("build_status") not in ("cross_domain", "complete"):
            self.taxonomy["build_status"] = "bfs"
            self._save_output()

            queue = self._find_pending_nodes()
            total_split = self._run_bfs(queue, splitter)

            self._check_cancelled()
            self.taxonomy["build_status"] = "cross_domain"
            self._save_output()
            logger.info("BFS phase complete")
        else:
            total_split = 0
            logger.info("BFS phase already complete, skipping")

        # Phase 3: Cross-domain assignment
        self._check_cancelled()
        if self.config.enable_cross_domain and self.taxonomy.get("build_status") != "complete":
            self._apply_cross_domain(client)

        # Phase 4: Finalize
        self.taxonomy["build_status"] = "complete"
        self._save_output()

        elapsed = time.time() - start_time
        self._print_summary(elapsed, total_split)

    # =========================================================================
    # Resume Decision
    # =========================================================================

    def _evaluate_resume(self) -> str:
        """Decide what to do for resume mode.

        Returns:
            "skip"    — completed taxonomy with matching config
            "resume"  — incomplete taxonomy with matching config
            "rebuild" — no valid checkpoint or config mismatch
        """
        output_dir = Path(self.config.output_dir)
        taxonomy_path = output_dir / "taxonomy.json"
        config_path = output_dir / "build_config.json"

        if not taxonomy_path.exists() or not config_path.exists():
            return "rebuild"

        if not self.config.matches_saved_config(str(config_path)):
            logger.info("Build config mismatch — cannot resume")
            return "rebuild"

        try:
            with open(taxonomy_path, 'r', encoding='utf-8') as f:
                taxonomy = json.load(f)
            status = taxonomy.get("build_status", "complete")
            if status == "complete":
                return "skip"
            return "resume"
        except (json.JSONDecodeError, IOError):
            return "rebuild"

    # =========================================================================
    # Setup Helpers
    # =========================================================================

    # Build output files — only these are deleted on clean; other files (service.json, etc.) are preserved
    _BUILD_FILES = frozenset({"taxonomy.json", "class.json", "keywords.json", "build_config.json", "assignments.json"})

    def _clean_output(self, preserve_keywords: bool = False):
        """Delete build output files from output directory (preserves non-build files)."""
        output_dir = Path(self.config.output_dir)
        if not output_dir.exists():
            return

        skip = {"keywords.json"} if preserve_keywords else set()
        for name in self._BUILD_FILES - skip:
            p = output_dir / name
            if p.exists():
                p.unlink()
        logger.info("Cleaned build outputs%s: %s",
                    " (preserved keywords.json)" if preserve_keywords else "", output_dir)

    def _init_taxonomy(self):
        """Initialize empty taxonomy with root containing all service IDs."""
        all_ids = sorted(self.services_index.keys())
        self.taxonomy = {
            "version": "2.0-hierarchical",
            "root": "root",
            "categories": {
                "root": {"children": [], "services": all_ids}
            }
        }
        self.class_data = {
            "version": "2.0-hierarchical",
            "categories": {
                "root": {
                    "name": "All API Services",
                    "description": "Root node containing all API services across all functional domains",
                }
            }
        }

    def _load_existing_output(self):
        """Load taxonomy, class data, and assignments from a previous partial build."""
        output_dir = Path(self.config.output_dir)
        with open(output_dir / "taxonomy.json", 'r', encoding='utf-8') as f:
            self.taxonomy = json.load(f)
        with open(output_dir / "class.json", 'r', encoding='utf-8') as f:
            self.class_data = json.load(f)
        assignments_path = output_dir / "assignments.json"
        if assignments_path.exists():
            with open(assignments_path, 'r', encoding='utf-8') as f:
                self.all_assignments = json.load(f)
        logger.info("Loaded existing state: %d categories",
                    len(self.taxonomy.get('categories', {})))

    def _load_services_index(self):
        """Load services from JSON file into services_index."""
        logger.info("Loading services from %s...", self.config.service_path)
        with open(self.config.service_path, 'r', encoding='utf-8') as f:
            services = json.load(f)
        self.services_index = {s['id']: s for s in services}
        logger.info("Loaded %d services", len(self.services_index))

    # =========================================================================
    # BFS Splitting
    # =========================================================================

    def _find_pending_nodes(self) -> deque:
        """Scan taxonomy for oversized leaf nodes that need splitting.

        Works for both fresh build (root has all services, no children → root enters queue)
        and resume (only unsplit oversized leaves enter queue).
        """
        max_depth = self.config.max_depth
        queue = deque()

        def scan(node_id, depth):
            cat = self.taxonomy['categories'].get(node_id, {})
            children = cat.get('children', [])
            if children:
                for child_id in children:
                    scan(child_id, depth + 1)
            elif len(cat.get('services', [])) > self.config.max_service_size:
                if max_depth is None or depth < max_depth:
                    queue.append((node_id, depth))

        scan('root', 0)
        return queue

    def _run_bfs(self, queue: deque, splitter: NodeSplitter) -> int:
        """Run BFS splitting loop. Returns number of nodes split."""
        total_split = 0
        queue_processed = 0
        max_depth = self.config.max_depth

        while queue:
            node_id, depth = queue.popleft()
            cat_data = self.taxonomy['categories'].get(node_id, {})
            service_ids = cat_data.get('services', [])

            # Get full service dicts
            node_services = [
                self.services_index[sid]
                for sid in service_ids
                if sid in self.services_index
            ]

            if not node_services:
                logger.info("Skipping %s: no valid services found in index", node_id)
                continue

            is_root = (node_id == 'root')
            parent_info = self._get_node_info(node_id)

            queue_processed += 1
            logger.info("SPLITTING [%d, %d queued]: %s (%s) - %d services (depth=%d)",
                        queue_processed, len(queue), node_id,
                        parent_info.get('name', 'Unknown'), len(node_services), depth)

            result = splitter.split_node(node_id, parent_info, node_services, is_root=is_root)

            # Apply result to taxonomy
            self._apply_split_result(node_id, result)
            self._accumulate_assignments(result)
            total_split += 1

            # Intermediate save after each split
            self._save_output()
            logger.info("Intermediate save complete")

            # Queue oversized children for further splitting
            child_depth = depth + 1
            for sub_id in sorted(result.subcategories.keys()):
                sub_services = self.taxonomy['categories'].get(sub_id, {}).get('services', [])
                if len(sub_services) > self.config.max_service_size:
                    if max_depth is None or child_depth < max_depth:
                        queue.append((sub_id, child_depth))
                        logger.info("-> %s has %d services, queued for splitting (depth=%d)",
                                    sub_id, len(sub_services), child_depth)

        return total_split

    # =========================================================================
    # Helpers
    # =========================================================================

    def _get_node_info(self, node_id: str) -> dict:
        """Get node metadata from class_data."""
        info = dict(self.class_data['categories'].get(node_id, {}))
        info['id'] = node_id
        return info

    def _apply_split_result(self, node_id: str, result: NodeSplitResult):
        """Apply a NodeSplitResult to the taxonomy and class_data."""
        sub_ids = sorted(result.subcategories.keys())

        # Build service lists per subcategory
        sub_services: Dict[str, List[str]] = {sub_id: [] for sub_id in sub_ids}
        for svc_id, assignment in result.assignments.items():
            for cat_id in assignment.get('category_ids', []):
                if cat_id in sub_services:
                    sub_services[cat_id].append(svc_id)

        # Update parent node
        self.taxonomy['categories'][node_id]['children'] = sub_ids
        self.taxonomy['categories'][node_id]['services'] = sorted(result.unclassified_service_ids)

        # Create subcategory entries
        for sub_id in sub_ids:
            self.taxonomy['categories'][sub_id] = {
                'children': [],
                'services': sorted(sub_services.get(sub_id, [])),
            }
            self.class_data['categories'][sub_id] = {
                'name': result.subcategories[sub_id]['name'],
                'description': result.subcategories[sub_id]['description'],
            }
            if result.subcategories[sub_id].get('boundary'):
                self.class_data['categories'][sub_id]['boundary'] = result.subcategories[sub_id]['boundary']
            if result.subcategories[sub_id].get('decision_rule'):
                self.class_data['categories'][sub_id]['decision_rule'] = result.subcategories[sub_id]['decision_rule']

        # Log results
        logger.info("Split result for %s: %d subcategories created, %d unclassified remain at parent",
                    node_id, len(sub_ids), len(result.unclassified_service_ids))
        for sub_id in sub_ids:
            n = len(sub_services.get(sub_id, []))
            name = result.subcategories[sub_id]['name']
            logger.info("  %s (%s): %d services", sub_id, name, n)

    def _accumulate_assignments(self, result: NodeSplitResult):
        """Accumulate assignments from a split result."""
        self.all_assignments.update(result.assignments)

    # =========================================================================
    # Cross-Domain
    # =========================================================================

    def _apply_cross_domain(self, client: LLMClient):
        """Run cross-domain multi-parent assignment on the main taxonomy."""
        logger.info("Cross-Domain Multi-Parent Assignment")

        assigner = CrossDomainAssigner(client, self.config.workers)
        additions = assigner.assign(self.taxonomy, self.class_data, self.services_index)

        if not additions:
            logger.info("No cross-domain additions")
            return

        added_count = 0
        for svc_id, target_cat_ids in additions.items():
            for cat_id in target_cat_ids:
                cat_data = self.taxonomy['categories'].get(cat_id)
                if cat_data is not None:
                    services = cat_data.setdefault('services', [])
                    if svc_id not in services:
                        services.append(svc_id)
                        services.sort()
                        added_count += 1

        logger.info("Cross-domain: added %d service-category links for %d services",
                    added_count, len(additions))

    # =========================================================================
    # Output
    # =========================================================================

    def _save_output(self):
        """Save taxonomy.json, class.json, assignments.json, and build_config.json."""
        output_dir = Path(self.config.output_dir)
        output_dir.mkdir(parents=True, exist_ok=True)

        with open(output_dir / "taxonomy.json", 'w', encoding='utf-8') as f:
            json.dump(self.taxonomy, f, indent=2, ensure_ascii=False)

        with open(output_dir / "class.json", 'w', encoding='utf-8') as f:
            json.dump(self.class_data, f, indent=2, ensure_ascii=False)

        if self.all_assignments:
            with open(output_dir / "assignments.json", 'w', encoding='utf-8') as f:
                json.dump(self.all_assignments, f, indent=2, ensure_ascii=False)

        # Stamp the service hash so RegistryService can detect when taxonomy is outdated
        self.config.service_hash = self._compute_service_hash()
        self.config.save(str(output_dir / "build_config.json"))

    def _compute_service_hash(self) -> str:
        """Hash (name, description) pairs of all services — order-independent.

        Mirrors _compute_build_hash in src/register/service.py.
        Only these two fields drive taxonomy structure, so only they matter for
        detecting whether a rebuild is needed.
        """
        pairs = sorted(
            (s["name"], s.get("description", ""))
            for s in self.services_index.values()
        )
        return hashlib.sha256(json.dumps(pairs, ensure_ascii=False).encode()).hexdigest()

    def _print_summary(self, elapsed: float, total_split: int):
        """Print final build summary."""
        depth_counts = {}
        total_services = 0

        def count_depth(cat_id, depth):
            nonlocal total_services
            cat_data = self.taxonomy['categories'].get(cat_id, {})
            services = cat_data.get('services', [])
            children = cat_data.get('children', [])
            total_services += len(services)
            if cat_id != 'root':
                depth_counts[depth] = depth_counts.get(depth, 0) + 1
            for child_id in children:
                count_depth(child_id, depth + 1)

        count_depth('root', 0)

        n_cats = len(self.taxonomy['categories']) - 1  # exclude root
        n_leaf = sum(
            1 for cat_id, cat_data in self.taxonomy['categories'].items()
            if cat_id != 'root' and not cat_data.get('children')
        )
        max_depth = max(depth_counts.keys()) if depth_counts else 0

        logger.info("BUILD COMPLETE")
        logger.info("Nodes split: %d", total_split)
        logger.info("Total categories: %d (including %d leaf nodes)", n_cats, n_leaf)
        logger.info("Max depth: %d", max_depth)
        logger.info("Depth distribution: %s", dict(sorted(depth_counts.items())))
        logger.info("Total service assignments: %d", total_services)
        logger.info("Elapsed: %.1fs", elapsed)

        logger.info("Top-level categories:")
        root_children = self.taxonomy['categories'].get('root', {}).get('children', [])
        for cat_id in sorted(root_children):
            cat_info = self.class_data['categories'].get(cat_id, {})
            cat_data = self.taxonomy['categories'].get(cat_id, {})
            n_direct = len(cat_data.get('services', []))
            children = cat_data.get('children', [])
            if children:
                n_sub_services = sum(
                    len(self.taxonomy['categories'].get(cid, {}).get('services', []))
                    for cid in children
                )
                logger.info("  %s: %d subcategories, %d direct + %d in subs",
                            cat_info.get('name', cat_id), len(children), n_direct, n_sub_services)
            else:
                logger.info("  %s: %d services (leaf)", cat_info.get('name', cat_id), n_direct)
