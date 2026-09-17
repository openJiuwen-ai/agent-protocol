"""Curated default queries per dataset, bilingual (Chinese + English).

Each query is selected from evaluation results where A2X get_important recall
> vector top-5 recall, highlighting scenarios where hierarchical taxonomy
navigation outperforms flat vector retrieval.

Query files live at: database/{dataset}/query/default_queries.json
"""

import json

from a2x_registry.common.paths import database_dir, get_home


def get_default_queries(dataset: str = "ToolRet_clean") -> tuple:
    """Return (queries, source) for the given dataset.

    *source* is the resolved file path (after following ``$ref``), so callers
    can tell whether two datasets share the same query pool.

    Supports ``{"$ref": "relative/path.json"}`` files that redirect to a shared
    query list so that variant datasets (e.g. CN) reuse the same defaults.
    """
    home = get_home()
    path = database_dir() / dataset / "query" / "default_queries.json"
    if not path.exists():
        return [], ""
    with open(path, encoding="utf-8") as f:
        data = json.load(f)
    source = str(path.relative_to(home)).replace("\\", "/")
    # Follow $ref redirect (one level)
    if isinstance(data, dict) and "$ref" in data:
        ref_path = home / data["$ref"]
        source = data["$ref"].replace("\\", "/")
        if not ref_path.exists():
            return [], source
        with open(ref_path, encoding="utf-8") as f:
            data = json.load(f)
    return (data if isinstance(data, list) else []), source
