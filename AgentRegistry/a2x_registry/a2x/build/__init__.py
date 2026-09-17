# Build module for A2X taxonomy construction
#
# Fully automatic: keyword extraction → category design → classify/refine → subdivision
# CLI: python -m a2x_registry.a2x.build

from .config import AutoHierarchicalConfig
from .taxonomy_builder import TaxonomyBuilder
