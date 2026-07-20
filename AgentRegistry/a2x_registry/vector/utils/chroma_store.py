"""通用 ChromaDB 向量存储"""

from typing import List, Dict, Any, Optional
import chromadb


class ChromaStore:
    """ChromaDB 向量存储封装

    提供文档的存储和相似度查询功能。

    Args:
        collection_name: 集合名称
        persist_dir: 持久化目录，默认 "database/chroma"

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
        """添加文档到集合

        Args:
            ids: 文档 ID 列表
            texts: 文档文本列表
            embeddings: 向量列表
            metadatas: 可选的元数据列表
        """
        self.collection.add(
            ids=ids,
            documents=texts,
            embeddings=embeddings,
            metadatas=metadatas
        )

    def query(self, embedding: List[float], top_k: int) -> Dict[str, Any]:
        """查询最相似的文档

        Args:
            embedding: 查询向量
            top_k: 返回结果数量

        Returns:
            包含 ids, documents, distances 的字典
        """
        return self.collection.query(
            query_embeddings=[embedding],
            n_results=top_k
        )

    def count(self) -> int:
        """返回集合中的文档数量"""
        return self.collection.count()

    def get_all_docs(self) -> dict:
        """返回集合中所有文档的 {id: document_text} 映射"""
        result = self.collection.get()
        return dict(zip(result["ids"], result["documents"]))

    def upsert(
        self,
        ids: List[str],
        texts: List[str],
        embeddings: List[List[float]],
        metadatas: Optional[List[Dict[str, Any]]] = None,
    ) -> None:
        """插入或更新文档（id 已存在则覆盖，不存在则新增）"""
        self.collection.upsert(
            ids=ids,
            documents=texts,
            embeddings=embeddings,
            metadatas=metadatas,
        )

    def delete_ids(self, ids: List[str]) -> None:
        """按 ID 删除文档"""
        if ids:
            self.collection.delete(ids=ids)

    @property
    def stored_embedding_model(self) -> str | None:
        """Return the embedding_model recorded in collection metadata, or None."""
        return (self.collection.metadata or {}).get("embedding_model")

    def clear(self) -> None:
        """清空集合"""
        ids = self.collection.get()["ids"]
        if ids:
            self.collection.delete(ids=ids)
