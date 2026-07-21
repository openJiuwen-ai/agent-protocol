"""通用检索评估指标"""

from typing import List, Set
import numpy as np


def precision_at_k(retrieved: List[str], relevant: Set[str], k: int) -> float:
    """计算 Precision@K

    P@K = |retrieved[:k] ∩ relevant| / k

    Args:
        retrieved: 检索结果 ID 列表（按相关性排序）
        relevant: 相关文档 ID 集合
        k: 截断位置

    Returns:
        Precision@K 值，范围 [0, 1]
    """
    return len(set(retrieved[:k]) & relevant) / k


def recall_at_k(retrieved: List[str], relevant: Set[str], k: int) -> float:
    """计算 Recall@K

    R@K = |retrieved[:k] ∩ relevant| / |relevant|

    Args:
        retrieved: 检索结果 ID 列表
        relevant: 相关文档 ID 集合
        k: 截断位置

    Returns:
        Recall@K 值，范围 [0, 1]
    """
    if not relevant:
        return 0.0
    return len(set(retrieved[:k]) & relevant) / len(relevant)


def hit_at_k(retrieved: List[str], relevant: Set[str], k: int) -> float:
    """计算 Hit@K

    Hit@K = 1 如果 top-k 中有任意相关文档，否则为 0

    Args:
        retrieved: 检索结果 ID 列表
        relevant: 相关文档 ID 集合
        k: 截断位置

    Returns:
        1.0 或 0.0
    """
    return 1.0 if set(retrieved[:k]) & relevant else 0.0


def mrr(retrieved: List[str], relevant: Set[str]) -> float:
    """计算 Mean Reciprocal Rank

    MRR = 1 / (第一个相关文档的排名)

    Args:
        retrieved: 检索结果 ID 列表
        relevant: 相关文档 ID 集合

    Returns:
        MRR 值，范围 [0, 1]
    """
    for i, doc_id in enumerate(retrieved, 1):
        if doc_id in relevant:
            return 1.0 / i
    return 0.0


def ndcg_at_k(retrieved: List[str], relevant: Set[str], k: int) -> float:
    """计算 NDCG@K (Normalized Discounted Cumulative Gain)

    DCG@K = Σ rel_i / log2(i+1)
    NDCG@K = DCG@K / IDCG@K

    Args:
        retrieved: 检索结果 ID 列表
        relevant: 相关文档 ID 集合
        k: 截断位置

    Returns:
        NDCG@K 值，范围 [0, 1]
    """
    # 计算 DCG
    dcg = sum(
        1.0 / np.log2(i + 1)
        for i, doc_id in enumerate(retrieved[:k], 1)
        if doc_id in relevant
    )

    # 计算 IDCG (理想情况：所有相关文档排在最前)
    n_relevant = min(k, len(relevant))
    idcg = sum(1.0 / np.log2(i + 1) for i in range(1, n_relevant + 1))

    return dcg / idcg if idcg > 0 else 0.0
