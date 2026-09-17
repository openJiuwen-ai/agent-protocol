"""Service for reading taxonomy tree structure for frontend visualization."""

import json
from typing import Dict

from a2x_registry.common.paths import dataset_dir

# In-memory cache: dataset → tree dict
_cache: Dict[str, Dict] = {}


def get_taxonomy_tree(dataset: str = "ToolRet_clean") -> Dict:
    """Load taxonomy and class data, return a tree structure for D3.js.

    Results are cached in memory after first load.
    """
    if dataset in _cache:
        return _cache[dataset]

    ds_dir = dataset_dir(dataset)
    taxonomy_path = ds_dir / "taxonomy" / "taxonomy.json"
    class_path = ds_dir / "taxonomy" / "class.json"

    with open(taxonomy_path, "r", encoding="utf-8") as f:
        taxonomy = json.load(f)
    with open(class_path, "r", encoding="utf-8") as f:
        classes = json.load(f).get("categories", {})

    categories = taxonomy.get("categories", {})
    root_id = taxonomy.get("root", "root")

    def build_node(cat_id: str) -> Dict:
        cat_data = categories.get(cat_id, {})
        cat_class = classes.get(cat_id, {})
        children_ids = cat_data.get("children", [])
        service_ids = cat_data.get("services", [])
        node = {
            "id": cat_id,
            "name": cat_class.get("name", cat_id),
            "description": cat_class.get("description", ""),
            "service_count": len(service_ids),
            "children": [build_node(cid) for cid in children_ids],
        }
        if service_ids and not children_ids:
            node["services"] = service_ids
        return node

    tree = build_node(root_id)
    _cache[dataset] = tree
    return tree
