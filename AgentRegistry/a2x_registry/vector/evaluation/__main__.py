"""CLI entry point: python -m a2x_registry.vector.evaluation

Usage:
    python -m a2x_registry.vector.evaluation --max-queries 50
    python -m a2x_registry.vector.evaluation --service-path database/publicMCP/service.json \
        --query-file database/publicMCP/query/query_cn.json --top-k 10
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

    from a2x_registry.vector.evaluation.vector_evaluator import VectorEvaluator

    logging.basicConfig(level=logging.INFO, format="%(message)s")

    parser = argparse.ArgumentParser(description="Evaluate vector search")
    parser.add_argument("--query-file", default="database/ToolRet_clean/query/query.json",
                        help="Path to query file")
    parser.add_argument("--service-path", default="database/ToolRet_clean/service.json",
                        help="Path to service.json")
    parser.add_argument("--collection-name", default="toolret_new",
                        help="ChromaDB collection name")
    parser.add_argument("--top-k", type=int, default=10,
                        help="Number of results to retrieve (default: 10)")
    parser.add_argument("--max-queries", type=int, default=None,
                        help="Max queries to evaluate (default: all)")
    parser.add_argument("--output", default=None,
                        help="Output directory (auto-generated if omitted)")
    parser.add_argument("--top-k-list", type=str, default=None,
                        help="Comma-separated K values for multi-K metrics (e.g. '5,10')")
    parser.add_argument("--model-name", default=None,
                        help="Embedding model name (auto-read from vector_config.json if omitted)")
    parser.add_argument("--force-rebuild", action="store_true",
                        help="Force rebuild vector index")
    args = parser.parse_args()

    # Auto-detect model from vector_config.json if not specified
    model_name = args.model_name
    if model_name is None:
        import json
        dataset_dir = Path(args.service_path).parent
        vc_path = dataset_dir / "vector_config.json"
        if vc_path.exists():
            with open(vc_path, encoding="utf-8") as f:
                model_name = json.load(f).get("embedding_model", "all-MiniLM-L6-v2")
        else:
            model_name = "all-MiniLM-L6-v2"
    logging.info("Embedding model: %s", model_name)

    if args.output is None:
        args.output = generate_output_dir(
            method="vector",
            service_path=args.service_path,
            query_file=args.query_file,
            max_queries=args.max_queries,
        )

    top_k_list = None
    if args.top_k_list:
        top_k_list = [int(x.strip()) for x in args.top_k_list.split(",")]

    evaluator = VectorEvaluator(
        service_path=args.service_path,
        collection_name=args.collection_name,
        top_k=args.top_k,
        top_k_list=top_k_list,
        force_rebuild=args.force_rebuild,
        model_name=model_name,
    )

    evaluator.evaluate_batch(
        query_file=args.query_file,
        max_queries=args.max_queries,
        output_dir=args.output,
    )


if __name__ == "__main__":
    main()
