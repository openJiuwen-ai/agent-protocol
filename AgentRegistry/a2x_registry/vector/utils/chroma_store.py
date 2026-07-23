"""General-purpose ChromaDB vector store."""

from typing import List, Dict, Any, Optional
import chromadb


class ChromaStore:
    """ChromaDB vector store wrapper.

    Provides document storage and similarity-query functionality.

    Args:
        collection_name: collection name
        persist_dir: persistence directory, default "database/chroma"

    Example:
        >>> store = ChromaStore("my_collection")
        >>> store.add(["id1"], ["text1"], [[0.1, 0.2, ...]])
        >>> results = store.query([0.1, 0.2, ...], top_k=5)
    """

    def __init__(self, collection_name: str, persist_dir: str = "database/chroma",
                 embedding_model: str | None = None):
        self.client = chromadb.PersistentClient(path=persist_dir)
        meta = {"hnsw:space": "cosine"}
        if embedding_model:
            meta["embedding_model"] = embedding_model
        self.collection = self.client.get_or_create_collection(
            name=collection_name,
            metadata=meta,
        )

    def add(
        self,
        ids: List[str],
        texts: List[str],
        embeddings: List[List[float]],
        metadatas: Optional[List[Dict[str, Any]]] = None
    ) -> None:
        """Add documents to the collection.

        Args:
            ids: list of document IDs
            texts: list of document texts
            embeddings: list of vectors
            metadatas: optional list of metadata
        """
        self.collection.add(
            ids=ids,
            documents=texts,
            embeddings=embeddings,
            metadatas=metadatas
        )

    def query(self, embedding: List[float], top_k: int) -> Dict[str, Any]:
        """Query the most similar documents.

        Args:
            embedding: query vector
            top_k: number of results to return

        Returns:
            dict containing ids, documents, distances
        """
        return self.collection.query(
            query_embeddings=[embedding],
            n_results=top_k
        )

    def count(self) -> int:
        """Return the number of documents in the collection."""
        return self.collection.count()

    def get_all_docs(self) -> dict:
        """Return a {id: document_text} mapping of all documents in the collection."""
        result = self.collection.get()
        return dict(zip(result["ids"], result["documents"]))

    def upsert(
        self,
        ids: List[str],
        texts: List[str],
        embeddings: List[List[float]],
        metadatas: Optional[List[Dict[str, Any]]] = None,
    ) -> None:
        """Insert or update documents (overwrite if id exists, insert otherwise)."""
        self.collection.upsert(
            ids=ids,
            documents=texts,
            embeddings=embeddings,
            metadatas=metadatas,
        )

    def delete_ids(self, ids: List[str]) -> None:
        """Delete documents by ID."""
        if ids:
            self.collection.delete(ids=ids)

    @property
    def stored_embedding_model(self) -> str | None:
        """Return the embedding_model recorded in collection metadata, or None."""
        return (self.collection.metadata or {}).get("embedding_model")

    def clear(self) -> None:
        """Clear the collection."""
        ids = self.collection.get()["ids"]
        if ids:
            self.collection.delete(ids=ids)
