"""Configuration for fully-automatic hierarchical taxonomy building."""

from dataclasses import dataclass, asdict
from pathlib import Path
from typing import Optional
import json


@dataclass
class AutoHierarchicalConfig:
    """Configuration for the auto-hierarchical taxonomy builder.

    Unified BFS recursive process:
    1. Root node: keyword/description-based category design -> classify/refine
    2. Recursive subdivision: BFS splits oversized nodes (> max_service_size)
    3. Cross-domain multi-parent assignment

    Convention: The parent directory of service_path is used as the dataset name.
    For example, "database/ToolRet_clean/service.json" → dataset name "ToolRet_clean".
    If output_dir is not specified, it defaults to "database/{dataset_name}".
    """

    # Paths
    service_path: str = "database/ToolRet_clean/service.json"
    output_dir: str = None  # Default: "database/{dataset_name}"

    # Keyword extraction
    keyword_batch_size: int = 50
    max_keywords_per_service: int = 5
    temperature_keywords: float = 0.0

    # Category design
    temperature_design: float = 0.0

    # Classify/refine
    generic_ratio: float = 1 / 3  # services matching > this ratio of subcategories are "generic"
    delete_threshold: int = 2  # after iterations, subcategories with <= this many services are deleted
    classification_retries: int = 2
    max_refine_iterations: int = 3
    temperature_classify: float = 0.0

    # Keyword vs description threshold
    keyword_threshold: int = 500

    # Tree structure
    max_service_size: int = 40  # max services per leaf node
    max_categories_size: int = 20  # max subcategories per node split
    max_depth: int = 3
    min_leaf_size: int = 5  # min services for a leaf to remain standalone

    # Parallelism
    workers: int = 20

    # LLM max_tokens for different operations
    max_tokens_design: int = 6000       # category design (keyword-based / root redesign)
    max_tokens_design_small: int = 4000 # category design (description-based / child refine)
    max_tokens_classify: int = 300      # single service classification
    max_tokens_validate: int = 3000     # root category validation
    max_tokens_keywords: int = 4000     # keyword extraction per batch

    # Cross-domain
    enable_cross_domain: bool = True

    # Set at build-end: SHA256 hash of (name, description) pairs from service.json.
    # Used by RegistryService to detect whether the taxonomy is still valid.
    # Not a build parameter — excluded from config comparison.
    service_hash: Optional[str] = None

    # Fields that don't affect build results (excluded from config comparison)
    _NON_BUILD_FIELDS = frozenset({"output_dir", "workers", "service_hash"})

    def __post_init__(self):
        if self.output_dir is None:
            self.output_dir = f"database/{self.dataset_name}/taxonomy"

    @property
    def dataset_name(self) -> str:
        """Dataset name derived from service_path parent directory."""
        return Path(self.service_path).parent.name

    def get_max_categories(self) -> int:
        """Max subcategories per node split."""
        return self.max_categories_size

    def build_params(self) -> dict:
        """Return only the parameters that affect build results.

        Excludes output_dir and workers — these don't change the taxonomy output.
        service_path is included but compared by dataset name in matches_saved_config().
        """
        return {k: v for k, v in asdict(self).items()
                if k not in self._NON_BUILD_FIELDS}

    def matches_saved_config(self, saved_config_path: str) -> bool:
        """Check if this config's build parameters match a saved build_config.json.

        service_path is compared by dataset name (parent directory name) only,
        so different absolute paths to the same dataset will match.
        """
        try:
            with open(saved_config_path, 'r', encoding='utf-8') as f:
                saved = json.load(f)
        except (FileNotFoundError, json.JSONDecodeError):
            return False
        current = self.build_params()
        for key, value in current.items():
            if key not in saved:
                return False
            if key == "service_path":
                # Compare by dataset name only
                if Path(saved[key]).parent.name != Path(value).parent.name:
                    return False
                continue
            # Float comparison with tolerance (1e-3 for CLI rounding)
            if isinstance(value, float):
                if abs(saved[key] - value) > 1e-3:
                    return False
            elif saved[key] != value:
                return False
        return True

    def to_dict(self) -> dict:
        return asdict(self)

    def save(self, path: str):
        with open(path, 'w', encoding='utf-8') as f:
            json.dump(self.to_dict(), f, indent=2, ensure_ascii=False)
