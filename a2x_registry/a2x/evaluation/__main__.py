"""CLI entry point: python -m a2x_registry.a2x.evaluation

Usage:
    python -m a2x_registry.a2x.evaluation --data-dir database/ToolRet_clean --max-queries 50
    python -m a2x_registry.a2x.evaluation --data-dir database/publicMCP \
        --query-file database/publicMCP/query/query_cn.json \
        --service-path database/publicMCP/service.json --mode get_one
"""

import argparse
import logging
import sys
from pathlib import Path

from a2x_registry.common import feature_flags
from a2x_registry.common.errors import FeatureNotInstalledError
from a2x_registry.common.naming import generate_output_dir


def main():
    for f in ("vector", "evaluation"):
        try:
            feature_flags.require(f)
        except FeatureNotInstalledError as exc:
            print(str(exc), file=sys.stderr)
            sys.exit(2)

    from a2x_registry.a2x.evaluation.a2x_evaluator import A2XEvaluator

    logging.basicConfig(level=logging.INFO, format="%(message)s")

    parser = argparse.ArgumentParser(description="Evaluate A2X taxonomy search")
    parser.add_argument("--data-dir", required=True,
                        help="Directory containing taxonomy.json and class.json")
    parser.add_argument("--query-file", default="database/ToolRet_clean/query/query.json",
                        help="Path to query file")
    parser.add_argument("--service-path", default="database/ToolRet_clean/service.json",
                        help="Path to service.json")
    parser.add_argument("--max-queries", type=int, default=None,
                        help="Max queries to evaluate (default: all)")
    parser.add_argument("--output", default=None,
                        help="Output directory (auto-generated if omitted)")
    parser.add_argument("--workers", type=int, default=10,
                        help="Parallel workers (default: 10)")
    parser.add_argument("--mode", choices=["get_all", "get_one", "get_important"], default="get_all",
                        help="Search mode: get_all (default), get_one (precision-focused), or get_important (confident-only)")
    parser.add_argument("--notes", default="")
    args = parser.parse_args()

    data_dir = Path(args.data_dir)

    if args.output is None:
        args.output = generate_output_dir(
            method="a2x",
            service_path=args.service_path,
            query_file=args.query_file,
            max_queries=args.max_queries,
            mode=args.mode,
        )

    evaluator = A2XEvaluator(
        max_workers=args.workers,
        taxonomy_path=str(data_dir / "taxonomy" / "taxonomy.json"),
        class_path=str(data_dir / "taxonomy" / "class.json"),
        service_path=args.service_path,
        mode=args.mode,
    )

    evaluator.evaluate_batch(
        query_file=args.query_file,
        max_queries=args.max_queries,
        output_dir=args.output,
        experiment_id=Path(args.output).name,
        notes=args.notes or f"workers={args.workers}",
    )


if __name__ == "__main__":
    main()
