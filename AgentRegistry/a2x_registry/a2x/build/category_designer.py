"""Category design and refinement for taxonomy nodes.

Two responsibilities:
- design(): Create initial subcategory scheme (keyword-based or description-based)
- refine(): Adjust subcategories based on classification feedback

Automatically selects the design approach based on service count:
- Large nodes (> keyword_threshold): keyword extraction -> design from keyword frequency
- Small nodes (<= keyword_threshold): design directly from service descriptions
"""

import json
import logging
from pathlib import Path
from typing import Dict, List, Optional

from a2x_registry.common.llm_client import LLMClient, parse_json_response

logger = logging.getLogger(__name__)
from .config import AutoHierarchicalConfig
from .keyword_extractor import KeywordExtractor
from .prompts import (
    format_services_for_prompt,
    format_categories_for_prompt,
    format_subcategory_stats,
    format_problem_services,
    SYSTEM_CATEGORY_DESIGN,
    CATEGORY_DESIGN_TEMPLATE,
    SYSTEM_DESIGN_FROM_DESCRIPTIONS,
    DESIGN_FROM_DESCRIPTIONS_TEMPLATE,
    SYSTEM_VALIDATE_ROOT_CATEGORIES,
    VALIDATE_ROOT_CATEGORIES_TEMPLATE,
    REDESIGN_VIOLATED_CATEGORIES_TEMPLATE,
    SYSTEM_REFINE_NODE,
    REFINE_SUBCATEGORIES_TEMPLATE,
    format_keywords_for_design,
    format_node_context_for_design,
)


class CategoryDesigner:
    """Category design and refinement for taxonomy nodes.

    - design_categories(): initial design (keyword-based or description-based)
    - refine_categories(): adjust based on classification feedback
    """

    def __init__(self, llm_client: LLMClient, config: AutoHierarchicalConfig):
        self.client = llm_client
        self.config = config
        self.keyword_extractor = KeywordExtractor(llm_client, config)

    def design_categories(
        self,
        services: List[Dict],
        node_info: Optional[dict] = None,
        is_root: bool = False,
        max_categories: Optional[int] = None,
    ) -> Dict[str, dict]:
        """Design initial categories for a node.

        Args:
            services: Service dicts within this node
            node_info: Current node info {name, description, id, ...}.
                       None for root node.
            is_root: Whether this is the root node
            max_categories: Max category count (defaults to config value)

        Returns:
            {cat_id: {name, description, boundary, decision_rule}}
        """
        if max_categories is None:
            max_categories = self.config.get_max_categories()

        parent_id = "cat" if is_root else node_info.get('id', 'sub') if node_info else "sub"
        # For root, don't pass node_info to design prompts
        design_node_info = None if is_root else node_info

        n_services = len(services)
        threshold = self.config.keyword_threshold

        strategy = 'keyword-based' if n_services > threshold else 'description-based'
        node_name = design_node_info.get('name', 'Unknown') if design_node_info else None
        logger.info("Category Design: %d services, threshold=%d, strategy=%s%s",
                     n_services, threshold, strategy,
                     f", node={node_name}" if node_name else "")

        if n_services > threshold:
            return self._design_from_keywords(
                services, design_node_info, parent_id, max_categories,
                is_root=is_root,
            )
        else:
            return self._design_from_descriptions(
                services, design_node_info, parent_id, max_categories
            )

    def _design_from_keywords(
        self,
        services: List[Dict],
        node_info: Optional[dict],
        parent_id: str,
        max_cats: int,
        is_root: bool = False,
    ) -> Dict[str, dict]:
        """Large node: extract keywords -> design categories from keyword frequency."""
        # Step 1: Extract keywords
        # keywords.json is a root-only cache — only root keywords are saved/reused.
        # Sub-node keywords are scoped to a different domain and must not be mixed.
        # Cache availability is controlled by the build's resume mode:
        #   resume="no"      → keywords.json deleted → no cache → fresh extraction
        #   resume="keyword" → keywords.json preserved → cache hit → skip extraction
        #   resume="yes"     → keywords.json preserved → cache hit → skip extraction
        keywords = None
        if is_root:
            keywords = self._load_cached_keywords()

        if keywords is None:
            keywords = self.keyword_extractor.extract(services, node_info=node_info)
            if is_root:
                self._save_keywords(keywords)

        self._last_keywords = keywords  # Save for validation retry

        # Step 2: Design categories from keywords
        categories = self._call_category_design(
            keywords, node_info, parent_id, max_cats, num_services=len(services),
        )
        self._print_categories(categories, "keyword-based")

        # Step 3: Validate root categories (LLM)
        if is_root:
            categories = self._validate_and_fix_root_categories(categories, keywords)

        return categories

    def _call_category_design(
        self,
        keywords: Dict[str, int],
        node_info: Optional[dict],
        parent_id: str,
        max_cats: int,
        num_services: int = 0,
    ) -> Dict[str, dict]:
        """Call LLM to design categories from keywords."""
        keywords_text = format_keywords_for_design(keywords)
        node_context_section = format_node_context_for_design(node_info)

        prompt = CATEGORY_DESIGN_TEMPLATE.format(
            n_keywords=len(keywords),
            n_services=num_services,
            keywords_text=keywords_text,
            node_context_section=node_context_section,
            max_cats=max_cats,
            parent_id=parent_id,
        )

        response = self.client.call(
            messages=[
                {"role": "system", "content": SYSTEM_CATEGORY_DESIGN},
                {"role": "user", "content": prompt},
            ],
            temperature=self.config.temperature_design,
            max_tokens=self.config.max_tokens_design,
        )

        if not response.success:
            raise RuntimeError(f"Category design from keywords failed: {response.error}")

        result = parse_json_response(response.content)
        if not result:
            raise RuntimeError("Failed to parse keyword-based category design response")

        return self._parse_categories(result)

    def _design_from_descriptions(
        self,
        services: List[Dict],
        node_info: Optional[dict],
        parent_id: str,
        max_cats: int,
    ) -> Dict[str, dict]:
        """Small node: design categories directly from service descriptions."""
        services_text = format_services_for_prompt(services)
        node_context_section = format_node_context_for_design(node_info)

        prompt = DESIGN_FROM_DESCRIPTIONS_TEMPLATE.format(
            service_count=len(services),
            node_context_section=node_context_section,
            max_cats=max_cats,
            parent_id=parent_id,
            services_text=services_text,
        )

        response = self.client.call(
            messages=[
                {"role": "system", "content": SYSTEM_DESIGN_FROM_DESCRIPTIONS},
                {"role": "user", "content": prompt},
            ],
            temperature=self.config.temperature_design,
            max_tokens=self.config.max_tokens_design_small,
        )

        if not response.success:
            raise RuntimeError(f"Category design from descriptions failed: {response.error}")

        result = parse_json_response(response.content)
        if not result:
            raise RuntimeError("Failed to parse description-based category design response")

        categories = self._parse_categories(result)
        self._print_categories(categories, "description-based")
        return categories

    def _load_cached_keywords(self) -> Optional[Dict[str, int]]:
        """Load root keywords from {output_dir}/keywords.json if available."""
        keywords_path = Path(self.config.output_dir) / "keywords.json"
        if not keywords_path.exists():
            logger.info("No cached keywords found at %s", keywords_path)
            return None
        try:
            with open(keywords_path, 'r', encoding='utf-8') as f:
                keywords = json.load(f)
            logger.info("Loaded %d keywords from cache: %s", len(keywords), keywords_path)
            return keywords
        except (json.JSONDecodeError, IOError) as e:
            logger.warning("Failed to load cached keywords: %s", e)
            return None

    def _save_keywords(self, keywords: Dict[str, int]):
        """Save root keywords to {output_dir}/keywords.json."""
        output_dir = Path(self.config.output_dir)
        output_dir.mkdir(parents=True, exist_ok=True)
        keywords_path = output_dir / "keywords.json"
        with open(keywords_path, 'w', encoding='utf-8') as f:
            json.dump(keywords, f, indent=2, ensure_ascii=False)
        logger.info("Saved %d keywords to %s", len(keywords), keywords_path)

    @staticmethod
    def _parse_categories(result: dict) -> Dict[str, dict]:
        """Parse LLM response into category dict."""
        key = 'subcategories' if 'subcategories' in result else 'categories'
        cat_list = result.get(key, [])
        if not cat_list:
            raise RuntimeError("Category design returned no categories")

        categories = {}
        for cat in cat_list:
            cat_id = cat.get('id', f"cat_{len(categories) + 1}")
            cat_data = {
                'name': cat.get('name', 'Unknown'),
                'description': cat.get('description', ''),
                'boundary': cat.get('boundary', ''),
                'decision_rule': cat.get('decision_rule', ''),
            }
            # Store associated_keywords if present (used for validation retry)
            if 'associated_keywords' in cat:
                cat_data['associated_keywords'] = cat['associated_keywords']
            categories[cat_id] = cat_data
        return categories

    @staticmethod
    def _print_categories(categories: Dict[str, dict], strategy: str):
        """Log designed categories summary."""
        lines = [f"Designed {len(categories)} categories ({strategy}):"]
        for cat_id, info in sorted(categories.items()):
            lines.append(f"  {cat_id}: {info['name']}")
        logger.info("\n".join(lines))

    # =========================================================================
    # Refinement
    # =========================================================================

    def refine_categories(
        self,
        parent_info: dict,
        subcategories: Dict[str, dict],
        assignments: Dict[str, dict],
        services: List[Dict],
        stats: dict,
        is_root: bool = False,
    ) -> Dict[str, dict]:
        """Refine subcategories based on classification feedback.

        Called by NodeSplitter during the classify/refine iteration loop
        when convergence has not been reached.
        """
        max_cats = self.config.get_max_categories()
        max_tokens = self.config.max_tokens_design if is_root else self.config.max_tokens_design_small
        n_subcats = len(subcategories)
        generic_threshold = max(1, int(n_subcats * self.config.generic_ratio))

        # Format tiny categories info
        tiny_cats = [
            (sub_id, stats['cat_counts'].get(sub_id, 0))
            for sub_id in subcategories
            if stats['cat_counts'].get(sub_id, 0) <= self.config.min_leaf_size
        ]
        if tiny_cats:
            tiny_lines = [f"\nTINY SUB-CATEGORIES (≤{self.config.min_leaf_size} services, need more services or merging):"]
            for sub_id, count in sorted(tiny_cats):
                tiny_lines.append(f"  {sub_id} ({subcategories[sub_id]['name']}): {count} services")
            tiny_cats_text = "\n".join(tiny_lines)
        else:
            tiny_cats_text = ""

        prompt = REFINE_SUBCATEGORIES_TEMPLATE.format(
            parent_name=parent_info.get('name', 'Unknown'),
            n_subcategories=n_subcats,
            current_subcategories_text=format_categories_for_prompt(subcategories),
            n_total=len(services),
            n_normal=stats['n_normal'],
            n_generic=stats['n_generic'],
            n_unclassified=stats['n_unclassified'],
            generic_threshold=generic_threshold,
            subcategory_stats_text=format_subcategory_stats(subcategories, assignments),
            tiny_cats_text=tiny_cats_text,
            problem_services_text=format_problem_services(
                services, assignments, subcategories, self.config.generic_ratio
            ),
            max_sub=max_cats,
            parent_id=parent_info.get('id', 'root'),
        )

        response = self.client.call(
            messages=[
                {"role": "system", "content": SYSTEM_REFINE_NODE},
                {"role": "user", "content": prompt},
            ],
            temperature=self.config.temperature_design,
            max_tokens=max_tokens,
        )

        if not response.success:
            logger.warning("Refinement LLM call failed: %s", response.error)
            return subcategories

        result = parse_json_response(response.content)
        if not result:
            logger.warning("Failed to parse refinement response")
            return subcategories

        changes = result.get('changes_summary', 'No summary')
        logger.info("Refinement: %s", changes)

        key = 'subcategories' if 'subcategories' in result else 'categories'
        cat_list = result.get(key, [])

        if not cat_list:
            logger.warning("Refinement returned no subcategories, keeping current")
            return subcategories

        # Parse into dict
        refined = {}
        for cat in cat_list:
            cat_id = cat.get('id', f"cat_{len(refined) + 1}")
            refined[cat_id] = {
                'name': cat.get('name', 'Unknown'),
                'description': cat.get('description', ''),
                'boundary': cat.get('boundary', ''),
                'decision_rule': cat.get('decision_rule', ''),
            }
        return refined

    # =========================================================================
    # Root Category Validation
    # =========================================================================

    def _validate_and_fix_root_categories(
        self,
        categories: Dict[str, dict],
        keywords: Dict[str, int],
        max_retries: int = 2,
    ) -> Dict[str, dict]:
        """Validate root categories and fix violations via LLM redesign.

        Uses LLM validation to audit each category against functional-domain rules.
        If violations found, call LLM to redistribute violated categories' keywords.
        """
        for attempt in range(max_retries):
            # LLM validation
            llm_violations = self._llm_validate_categories(categories)

            if not llm_violations:
                logger.info("Root category validation PASSED (attempt %d)", attempt + 1)
                return categories

            # Report violations
            violation_lines = [f"Root category validation FAILED (attempt {attempt + 1}/{max_retries}):"]
            for cat_id in sorted(llm_violations.keys()):
                name = categories[cat_id]['name']
                violation_lines.append(f"  {cat_id} ({name}): {llm_violations[cat_id]}")
            logger.info("\n".join(violation_lines))

            # Collect keywords from violated categories
            violated_keywords = {}
            for cat_id in llm_violations:
                for kw in categories[cat_id].get('associated_keywords', []):
                    if kw in keywords:
                        violated_keywords[kw] = keywords[kw]

            # If no associated_keywords stored, try to infer from keyword-category mapping
            if not violated_keywords:
                valid_keywords = set()
                for cat_id, info in categories.items():
                    if cat_id not in llm_violations:
                        valid_keywords.update(info.get('associated_keywords', []))
                violated_keywords = {
                    kw: count for kw, count in keywords.items()
                    if kw not in valid_keywords
                }

            # Fix: ask LLM to redesign
            categories = self._redesign_violated_categories(
                categories, set(llm_violations.keys()), violated_keywords
            )
            self._print_categories(categories, f"redesigned (attempt {attempt + 1})")

        return categories

    def _llm_validate_categories(self, categories: Dict[str, dict]) -> Dict[str, str]:
        """Ask LLM to validate each category against functional-domain rules.

        Returns: {cat_id: violation_reason}
        """
        categories_text = format_categories_for_prompt(categories)
        prompt = VALIDATE_ROOT_CATEGORIES_TEMPLATE.format(
            categories_text=categories_text,
        )

        response = self.client.call(
            messages=[
                {"role": "system", "content": SYSTEM_VALIDATE_ROOT_CATEGORIES},
                {"role": "user", "content": prompt},
            ],
            temperature=0.0,
            max_tokens=self.config.max_tokens_validate,
        )

        if not response.success:
            logger.warning("LLM validation call failed: %s", response.error)
            return {}

        result = parse_json_response(response.content)
        if not result or 'validations' not in result:
            logger.warning("Failed to parse LLM validation response")
            return {}

        violations = {}
        for v in result['validations']:
            if not v.get('valid', True):
                cat_id = v.get('id', '')
                if cat_id in categories:
                    reason = v.get('reason', v.get('violation_type', 'unknown'))
                    violations[cat_id] = reason

        return violations

    def _redesign_violated_categories(
        self,
        categories: Dict[str, dict],
        violated_ids: set,
        violated_keywords: Dict[str, int],
    ) -> Dict[str, dict]:
        """Ask LLM to redistribute violated categories' keywords into proper domains."""
        violated_cats = {cid: info for cid, info in categories.items() if cid in violated_ids}

        all_categories_text = format_categories_for_prompt(categories)
        violated_text = format_categories_for_prompt(violated_cats)

        if violated_keywords:
            kw_sorted = sorted(violated_keywords.items(), key=lambda x: -x[1])
            violated_kw_text = "\n".join(f"- {kw}: {count} services" for kw, count in kw_sorted)
        else:
            violated_kw_text = "(keywords not available — redistribute based on category descriptions)"

        prompt = REDESIGN_VIOLATED_CATEGORIES_TEMPLATE.format(
            all_categories_text=all_categories_text,
            violated_categories_text=violated_text,
            violated_keywords_text=violated_kw_text,
        )

        response = self.client.call(
            messages=[
                {"role": "system", "content": SYSTEM_CATEGORY_DESIGN},
                {"role": "user", "content": prompt},
            ],
            temperature=self.config.temperature_design,
            max_tokens=self.config.max_tokens_design,
        )

        if not response.success:
            logger.warning("Redesign LLM call failed, keeping current categories")
            return categories

        result = parse_json_response(response.content)
        if not result:
            logger.warning("Failed to parse redesign response, keeping current categories")
            return categories

        try:
            new_categories = self._parse_categories(result)
            if len(new_categories) < 3:
                logger.warning("Redesign returned only %d categories, keeping current", len(new_categories))
                return categories
            return new_categories
        except RuntimeError as e:
            logger.warning("Redesign parse error: %s, keeping current categories", e)
            return categories
