"""CLI entry point: python -m a2x_registry.traditional.evaluation

Usage:
    python -m a2x_registry.traditional.evaluation --max-queries 50
    python -m a2x_registry.traditional.evaluation --service-path database/publicMCP/service.json \
        --query-file database/publicMCP/query/query_cn.json
"""

import argparse
import logging
import sys

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

    from a2x_registry.traditional.evaluation.traditional_evaluator import TraditionalEvaluator

    logging.basicConfig(level=logging.INFO, format="%(message)s")

    parser = argparse.ArgumentParser(description="Evaluate Traditional (MCP-style) search")
    parser.add_argument("--query-file", default="database/ToolRet_clean/query/query.json",
                        help="Path to query file")
    parser.add_argument("--service-path", default="database/ToolRet_clean/service.json",
                        help="Path to service.json")
    parser.add_argument("--max-queries", type=int, default=None,
                        help="Max queries to evaluate (default: all)")
    parser.add_argument("--output", default=None,
                        help="Output directory (auto-generated if omitted)")
    parser.add_argument("--workers", type=int, default=10,
                        help="Number of parallel workers (default: 10)")
    args = parser.parse_args()

    if args.output is None:
        args.output = generate_output_dir(
            method="traditional",
            service_path=args.service_path,
            query_file=args.query_file,
            max_queries=args.max_queries,
        )

    evaluator = TraditionalEvaluator(
        service_path=args.service_path,
        max_workers=args.workers,
    )

    evaluator.evaluate_batch(
        query_file=args.query_file,
        max_queries=args.max_queries,
        output_dir=args.output,
    )


if __name__ == "__main__":
    main()
