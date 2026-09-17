"""Evaluation output directory naming convention.

Format: results/{date}_{method}-{mode}_{dataset}[-{suffix}]_{count}

Examples:
    results/20260323_a2x-getall_toolretnew_50
    results/20260323_a2x-getone_publicmcp-cn_50
    results/20260323_vector_toolretnew_1714
    results/20260323_traditional_publicmcp_50

Dataset name is derived from the parent directory of service_path.
Query suffix (e.g. "-cn") is derived from the query filename if not "query.json".
"""

import json
from datetime import datetime
from pathlib import Path


def generate_output_dir(
    method: str,
    service_path: str,
    query_file: str,
    max_queries: int = None,
    mode: str = None,
) -> str:
    """Generate a standardized output directory path.

    Args:
        method: Method name ("a2x", "vector", "traditional")
        service_path: Path to service.json (dataset name from parent dir)
        query_file: Path to query file (suffix from filename if not "query.json")
        max_queries: Max queries to evaluate (None = read actual count from file)
        mode: Search mode for a2x ("get_all", "get_one"), None for other methods

    Returns:
        Output directory path like "results/20260323_a2x-getone_publicmcp-cn_50"
    """
    date_str = datetime.now().strftime("%Y%m%d")

    # Method tag (e.g. "a2x-getone", "vector", "traditional")
    if mode:
        mode_short = mode.replace("_", "")
        method_tag = f"{method}-{mode_short}"
    else:
        method_tag = method

    # Dataset name from service_path parent dir
    dataset_name = Path(service_path).parent.name.lower().replace("_", "")

    # Query suffix from filename (e.g. "query_cn.json" → "-cn", "query.json" → "")
    query_stem = Path(query_file).stem  # "query" or "query_cn"
    if query_stem == "query":
        query_suffix = ""
    else:
        # "query_cn" → "-cn", "query_en" → "-en"
        query_suffix = "-" + query_stem.replace("query_", "").replace("query", "")

    # Query count
    if max_queries:
        query_count = max_queries
    else:
        with open(query_file, 'r', encoding='utf-8') as f:
            query_count = len(json.load(f))

    return f"results/{date_str}_{method_tag}_{dataset_name}{query_suffix}_{query_count}"
