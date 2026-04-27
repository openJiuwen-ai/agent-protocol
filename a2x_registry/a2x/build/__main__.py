"""CLI entry point: python -m a2x_registry.a2x.build"""

import argparse
import logging
import sys

from a2x_registry.common import feature_flags
from a2x_registry.common.errors import FeatureNotInstalledError


def main():
    try:
        feature_flags.require("vector")
    except FeatureNotInstalledError as exc:
        print(str(exc), file=sys.stderr)
        sys.exit(2)

    # Heavy imports deferred until after the feature gate so lite installs
    # see the friendly hint rather than a raw ImportError trace.
    from a2x_registry.a2x.build.config import AutoHierarchicalConfig
    from a2x_registry.a2x.build.taxonomy_builder import TaxonomyBuilder

    logging.basicConfig(level=logging.INFO, format="%(message)s")

    parser = argparse.ArgumentParser(
        description="Fully-automatic hierarchical taxonomy builder: "
                    "unified BFS recursive category design -> classify/refine -> subdivision"
    )
    parser.add_argument(
        "--service-path",
        default="database/ToolRet_clean/service.json",
        help="Path to service.json (default: database/ToolRet_clean/service.json)",
    )
    parser.add_argument(
        "--output-dir",
        default=None,
        help="Output directory for taxonomy (default: database/{dataset_name})",
    )
    parser.add_argument(
        "--keyword-batch-size",
        type=int,
        default=50,
        help="Services per batch for keyword extraction (default: 50)",
    )
    parser.add_argument(
        "--keyword-threshold",
        type=int,
        default=500,
        help="Service count threshold: >threshold uses keyword extraction, "
             "<=threshold uses direct descriptions (default: 500)",
    )
    parser.add_argument(
        "--max-service-size",
        type=int,
        default=40,
        help="Max services per node (default: 40)",
    )
    parser.add_argument(
        "--max-categories-size",
        type=int,
        default=20,
        help="Max subcategories per node (default: 20)",
    )
    parser.add_argument(
        "--generic-ratio",
        type=float,
        default=0.333,
        help="Services matching > this ratio of subcategories are 'generic' (default: 0.333)",
    )
    parser.add_argument(
        "--delete-threshold",
        type=int,
        default=2,
        help="After iterations, subcategories with <= this many services are deleted (default: 2)",
    )
    parser.add_argument(
        "--max-depth",
        type=int,
        default=3,
        help="Max taxonomy depth (default: 3, use 1 to skip subdivision)",
    )
    parser.add_argument(
        "--workers",
        type=int,
        default=20,
        help="Parallel workers for classification (default: 20)",
    )
    parser.add_argument(
        "--max-refine-iterations",
        type=int,
        default=3,
        help="Max refine iterations per node (default: 3)",
    )
    parser.add_argument(
        "--no-cross-domain",
        action="store_true",
        help="Disable cross-domain multi-parent assignment phase",
    )
    parser.add_argument(
        "--resume",
        choices=["no", "keyword", "yes"],
        default="no",
        help="Build mode: no=full rebuild, keyword=reuse cached keywords, yes=smart resume",
    )
    args = parser.parse_args()

    config = AutoHierarchicalConfig(
        service_path=args.service_path,
        output_dir=args.output_dir,
        keyword_batch_size=args.keyword_batch_size,
        keyword_threshold=args.keyword_threshold,
        generic_ratio=args.generic_ratio,
        delete_threshold=args.delete_threshold,
        max_service_size=args.max_service_size,
        max_categories_size=args.max_categories_size,
        max_depth=args.max_depth,
        workers=args.workers,
        max_refine_iterations=args.max_refine_iterations,
        enable_cross_domain=not args.no_cross_domain,
    )

    print("Auto-Hierarchical Taxonomy Builder")
    print("=" * 60)
    print(f"  Dataset: {config.dataset_name}")
    print(f"  Services: {config.service_path}")
    print(f"  Output: {config.output_dir}")
    print(f"  Resume: {args.resume}")
    print(f"  Keyword threshold: {config.keyword_threshold}")
    print(f"  Keyword batch size: {config.keyword_batch_size}")
    print(f"  Generic ratio: {config.generic_ratio:.3f}")
    print(f"  Delete threshold: {config.delete_threshold}")
    print(f"  Max service size: {config.max_service_size}")
    print(f"  Max categories size: {config.max_categories_size}")
    print(f"  Max depth: {config.max_depth or 'unlimited'}")
    print(f"  Workers: {config.workers}")
    print(f"  Max refine iterations: {config.max_refine_iterations}")
    print(f"  Cross-domain: {config.enable_cross_domain}")

    builder = TaxonomyBuilder(config)
    builder.build(resume=args.resume)


if __name__ == "__main__":
    main()
