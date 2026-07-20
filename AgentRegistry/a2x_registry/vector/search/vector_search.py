"""Vector search module.

Provides VectorSearch class with interface consistent with A2XSearch.

Usage:
    search = VectorSearch(service_path="database/ToolRet_clean/service.json")
    results = search.search(query, top_k=10)
"""

from dataclasses import dataclass

from a2x_registry.common.models import SearchResult  # noqa: F401
from a2x_registry.vector.utils.embedding import EmbeddingModel
from a2x_registry.vector.build.index_builder import IndexBuilder


@dataclass
class SearchStats:
    """Search statistics, matching A2X SearchStats interface."""
    llm_calls: int = 0
    total_tokens: int = 0


class VectorSearch:
    """Vector-based service retrieval using ChromaDB.

    Interface consistent with A2XSearch:
        search(query) -> (List[SearchResult], SearchStats)

    Args:
        service_path: Path to service.json
        collection_name: ChromaDB collection name
        persist_dir: ChromaDB persistence directory
        model_name: SentenceTransformer model name
    """

    def __init__(
        self,
        service_path: str = "database/ToolRet_clean/service.json",
        collection_name: str = "toolret_new",
        persist_dir: str = "database/chroma",
        model_name: str = "all-MiniLM-L6-v2",
        force_rebuild: bool = False,
        model: "EmbeddingModel | None" = None,
    ):
        self.model = model if model is not None else EmbeddingModel(model_name)

        # Build index if needed, reuse the same model instance
        builder = IndexBuilder(collection_name, persist_dir, model_name)
        self.store = builder.build(service_path, force_rebuild=force_rebuild, model=self.model)

        # Load service metadata for results
        import json
        with open(service_path, encoding="utf-8") as f:
            services = json.load(f)
        self.service_map = {s["id"]: s for s in services}

    def search(self, query: str, top_k: int = 10) -> tuple:
        """Search for relevant services.

        Args:
            query: User query string
            top_k: Number of results to return

        Returns:
            (List[SearchResult], SearchStats)
        """
        query_emb = self.model.encode(query, show_progress=False).tolist()
        raw = self.store.query(query_emb, top_k)

        results = []
        for sid in raw["ids"][0]:
            svc = self.service_map.get(sid, {})
            results.append(SearchResult(
                id=sid,
                name=svc.get("name", "Unknown"),
                description=svc.get("description", ""),
            ))

        stats = SearchStats(llm_calls=0, total_tokens=0)
        return results, stats
