"""Vector index builder.

Builds ChromaDB index from service descriptions using SentenceTransformer embeddings.

Usage:
    python -m a2x_registry.vector.build --service-path database/ToolRet_clean/service.json
"""

import json
import logging
from typing import List, Dict, Any

from a2x_registry.vector.utils import EmbeddingModel, ChromaStore

logger = logging.getLogger(__name__)


class IndexBuilder:
    """Builds and manages vector index for service retrieval.

    Args:
        collection_name: ChromaDB collection name
        persist_dir: ChromaDB persistence directory
        model_name: SentenceTransformer model name
    """

    def __init__(
        self,
        collection_name: str = "toolret_new",
        persist_dir: str = "database/chroma",
        model_name: str = "all-MiniLM-L6-v2",
    ):
        self.collection_name = collection_name
        self.persist_dir = persist_dir
        self.model_name = model_name

    def build(
        self,
        service_path: str,
        force_rebuild: bool = False,
        model: "EmbeddingModel | None" = None,
    ) -> ChromaStore:
        """Build vector index from service.json.

        Args:
            service_path: Path to service.json
            force_rebuild: If True, clear existing index and rebuild
            model: Optional pre-loaded EmbeddingModel to avoid redundant loading

        Returns:
            ChromaStore with indexed services
        """
        if model is None:
            model = EmbeddingModel(self.model_name)
        store = ChromaStore(self.collection_name, self.persist_dir,
                            embedding_model=self.model_name)

        if force_rebuild:
            logger.info("Clearing existing index...")
            store.clear()

        if store.count() == 0:
            services = self._load_services(service_path)
            logger.info(f"Building index for {len(services)} services...")

            ids = [s["id"] for s in services]
            texts = [s["description"] for s in services]

            logger.info("Encoding service descriptions...")
            embeddings = model.encode(texts, show_progress=True).tolist()

            logger.info("Adding to ChromaDB...")
            store.add(ids, texts, embeddings)
            logger.info(f"Index built: {store.count()} documents")
        else:
            logger.info(f"Using existing index: {store.count()} documents")

        return store

    @staticmethod
    def _load_services(path: str) -> List[Dict[str, Any]]:
        with open(path, encoding="utf-8") as f:
            return json.load(f)


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(description="Build vector index")
    parser.add_argument("--service-path", default="database/ToolRet_clean/service.json")
    parser.add_argument("--collection-name", default="toolret_new")
    parser.add_argument("--force-rebuild", action="store_true")
    args = parser.parse_args()

    builder = IndexBuilder(collection_name=args.collection_name)
    builder.build(args.service_path, force_rebuild=args.force_rebuild)
