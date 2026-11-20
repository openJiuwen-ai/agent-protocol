"""General-purpose retrieval evaluation metrics."""

from typing import List, Set
import numpy as np


def precision_at_k(retrieved: List[str], relevant: Set[str], k: int) -> float:
    """Compute Precision@K.

    P@K = |retrieved[:k] ∩ relevant| / k

    Args:
        retrieved: list of retrieved document IDs (ranked by relevance)
        relevant: set of relevant document IDs
        k: cutoff position

    Returns:
        Precision@K value, in [0, 1]
    """
    return len(set(retrieved[:k]) & relevant) / k


def recall_at_k(retrieved: List[str], relevant: Set[str], k: int) -> float:
    """Compute Recall@K.

    R@K = |retrieved[:k] ∩ relevant| / |relevant|

    Args:
        retrieved: list of retrieved document IDs
        relevant: set of relevant document IDs
        k: cutoff position

    Returns:
        Recall@K value, in [0, 1]
    """
    if not relevant:
        return 0.0
    return len(set(retrieved[:k]) & relevant) / len(relevant)


def hit_at_k(retrieved: List[str], relevant: Set[str], k: int) -> float:
    """Compute Hit@K.

    Hit@K = 1 if any relevant document is in the top-k, else 0.

    Args:
        retrieved: list of retrieved document IDs
        relevant: set of relevant document IDs
        k: cutoff position

    Returns:
        1.0 or 0.0
    """
    return 1.0 if set(retrieved[:k]) & relevant else 0.0


def mrr(retrieved: List[str], relevant: Set[str]) -> float:
    """Compute Mean Reciprocal Rank.

    MRR = 1 / (rank of the first relevant document)

    Args:
        retrieved: list of retrieved document IDs
        relevant: set of relevant document IDs

    Returns:
        MRR value, in [0, 1]
    """
    for i, doc_id in enumerate(retrieved, 1):
        if doc_id in relevant:
            return 1.0 / i
    return 0.0


def ndcg_at_k(retrieved: List[str], relevant: Set[str], k: int) -> float:
    """Compute NDCG@K (Normalized Discounted Cumulative Gain).

    DCG@K = Σ rel_i / log2(i+1)
    NDCG@K = DCG@K / IDCG@K

    Args:
        retrieved: list of retrieved document IDs
        relevant: set of relevant document IDs
        k: cutoff position

    Returns:
        NDCG@K value, in [0, 1]
    """
    # Compute DCG
    dcg = sum(
        1.0 / np.log2(i + 1)
        for i, doc_id in enumerate(retrieved[:k], 1)
        if doc_id in relevant
    )

    # Compute IDCG (ideal: all relevant documents ranked first)
    n_relevant = min(k, len(relevant))
    idcg = sum(1.0 / np.log2(i + 1) for i in range(1, n_relevant + 1))

    return dcg / idcg if idcg > 0 else 0.0
