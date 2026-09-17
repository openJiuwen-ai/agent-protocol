"""Vector search evaluator.

Interface consistent with A2XEvaluator:
    - evaluate_single_query(query_obj) -> QueryMetrics
    - evaluate_batch(query_file, max_queries, output_dir) -> (query_metrics, overall_metrics)
"""

import json
import logging
import time
from pathlib import Path
from dataclasses import dataclass, asdict
from typing import List, Dict
from tqdm import tqdm

logger = logging.getLogger(__name__)

from a2x_registry.vector.search.vector_search import VectorSearch
from a2x_registry.vector.utils.metrics import precision_at_k, recall_at_k, hit_at_k, mrr, ndcg_at_k
from a2x_registry.common.evaluation import compute_set_metrics


@dataclass
class QueryMetrics:
    """Per-query metrics, matching A2X QueryMetrics interface."""
    query_id: str
    query: str
    expected_count: int
    found_count: int
    correct_count: int
    precision: float
    recall: float
    hit: bool
    expected_tools: List[str]
    found_tools: List[str]
    correct_tools: List[str]
    missed_tools: List[str]
    wrong_tools: List[str]
    # Vector-specific: metrics at multiple K values
    metrics_at_k: Dict[str, Dict[str, float]] = None


@dataclass
class OverallMetrics:
    """Overall evaluation metrics, matching A2X OverallMetrics interface."""
    total_queries: int
    avg_precision: float
    avg_recall: float
    hit_rate: float
    avg_f1: float
    avg_found_services: float
    # Vector-specific: metrics at multiple K values
    metrics_at_k: Dict[str, Dict[str, float]] = None


class VectorEvaluator:
    """Evaluator for vector search, interface consistent with A2XEvaluator.

    Args:
        service_path: Path to service.json
        collection_name: ChromaDB collection name
        top_k: Default number of results for evaluation
        top_k_list: List of K values for multi-K metrics
    """

    def __init__(
        self,
        service_path: str = "database/ToolRet_clean/service.json",
        collection_name: str = "toolret_new",
        top_k: int = 10,
        top_k_list: List[int] = None,
        force_rebuild: bool = False,
        model_name: str = "all-MiniLM-L6-v2",
    ):
        self.top_k = top_k
        self.top_k_list = top_k_list or [5, 10]
        self.searcher = VectorSearch(
            service_path=service_path,
            collection_name=collection_name,
            model_name=model_name,
            force_rebuild=force_rebuild,
        )

    def evaluate_single_query(self, query_obj: Dict, top_k: int = None) -> QueryMetrics:
        """Evaluate a single query.

        Args:
            query_obj: Query object from query.json
            top_k: Override default top_k

        Returns:
            QueryMetrics for this query
        """
        k = top_k or self.top_k
        query_id = query_obj.get("id", "")
        query = query_obj.get("query", "")
        expected_tools = [t.get("id", "") for t in query_obj.get("correct_tools", [])]

        # Search with max K for multi-K evaluation
        max_k = max(self.top_k_list) if self.top_k_list else k
        results, _ = self.searcher.search(query, top_k=max_k)
        all_found = [r.id for r in results]

        # Primary metrics at default top_k
        found_tools = all_found[:k]
        correct, missed, wrong, precision, recall, hit = \
            compute_set_metrics(expected_tools, found_tools)
        expected_set = set(expected_tools)

        # Multi-K metrics
        metrics_at_k = {}
        for eval_k in self.top_k_list:
            metrics_at_k[str(eval_k)] = {
                "precision": precision_at_k(all_found, expected_set, eval_k),
                "recall": recall_at_k(all_found, expected_set, eval_k),
                "hit": hit_at_k(all_found, expected_set, eval_k),
                "mrr": mrr(all_found, expected_set),
                "ndcg": ndcg_at_k(all_found, expected_set, eval_k),
            }

        return QueryMetrics(
            query_id=query_id,
            query=query,
            expected_count=len(expected_tools),
            found_count=len(found_tools),
            correct_count=len(correct),
            precision=precision,
            recall=recall,
            hit=hit,
            expected_tools=expected_tools,
            found_tools=found_tools,
            correct_tools=correct,
            missed_tools=missed,
            wrong_tools=wrong,
            metrics_at_k=metrics_at_k,
        )

    def evaluate_batch(
        self,
        query_file: str,
        max_queries: int = None,
        output_dir: str = None,
        **kwargs,
    ) -> tuple:
        """Evaluate on multiple queries.

        Args:
            query_file: Path to query.json
            max_queries: Maximum number of queries (None = all)
            output_dir: Directory to save results

        Returns:
            (query_metrics, overall_metrics)
        """
        with open(query_file, 'r', encoding='utf-8') as f:
            queries = json.load(f)

        if max_queries is not None:
            queries = queries[:max_queries]

        logger.info(f"Evaluating {len(queries)} queries (top_k={self.top_k})...")

        start_time = time.time()
        query_metrics = []

        for q in tqdm(queries, desc="Evaluating", unit="query"):
            metrics = self.evaluate_single_query(q)
            query_metrics.append(metrics)

        elapsed = time.time() - start_time

        # Compute overall metrics
        n = len(query_metrics)
        if n == 0:
            overall = OverallMetrics(
                total_queries=0, avg_precision=0.0, avg_recall=0.0,
                hit_rate=0.0, avg_f1=0.0, avg_found_services=0.0, metrics_at_k={},
            )
            if output_dir:
                self._save_results(output_dir, query_metrics, overall, elapsed, query_file)
            return query_metrics, overall

        avg_precision = sum(m.precision for m in query_metrics) / n
        avg_recall = sum(m.recall for m in query_metrics) / n
        hit_rate = sum(1 for m in query_metrics if m.hit) / n
        avg_f1 = (2 * avg_precision * avg_recall / (avg_precision + avg_recall)
                  if avg_precision + avg_recall > 0 else 0.0)

        # Aggregate multi-K metrics
        agg_at_k = {}
        for k_str in query_metrics[0].metrics_at_k:
            agg_at_k[k_str] = {
                metric: sum(m.metrics_at_k[k_str][metric] for m in query_metrics) / n
                for metric in ["precision", "recall", "hit", "mrr", "ndcg"]
            }

        overall = OverallMetrics(
            total_queries=n,
            avg_precision=avg_precision,
            avg_recall=avg_recall,
            hit_rate=hit_rate,
            avg_f1=avg_f1,
            avg_found_services=sum(m.found_count for m in query_metrics) / n,
            metrics_at_k=agg_at_k,
        )

        # Print results
        self._print_results(overall, elapsed)

        # Save results
        if output_dir:
            self._save_results(output_dir, query_metrics, overall, elapsed, query_file)

        return query_metrics, overall

    def _print_results(self, overall: OverallMetrics, elapsed: float):
        logger.info("")
        logger.info("=" * 80)
        logger.info(f"Total Queries: {overall.total_queries}")
        logger.info(f"Time Elapsed: {elapsed:.2f}s")
        logger.info("")
        logger.info(f"Performance Metrics (top_k={self.top_k}):")
        logger.info(f"  Precision: {overall.avg_precision:.4f}")
        logger.info(f"  Recall:    {overall.avg_recall:.4f}")
        logger.info(f"  Hit Rate:  {overall.hit_rate:.4f}")
        logger.info(f"  F1 Score:  {overall.avg_f1:.4f}")
        logger.info("")
        logger.info(f"{'K':>5} | {'P@K':>10} | {'R@K':>10} | {'Hit@K':>10} | {'MRR':>10} | {'NDCG':>10}")
        logger.info("-" * 70)
        for k_str in sorted(overall.metrics_at_k, key=lambda x: int(x)):
            m = overall.metrics_at_k[k_str]
            logger.info(f"{k_str:>5} | {m['precision']:>10.4f} | {m['recall']:>10.4f} | "
                        f"{m['hit']:>10.4f} | {m['mrr']:>10.4f} | {m['ndcg']:>10.4f}")
        logger.info("=" * 80)

    def _save_results(self, output_dir, query_metrics, overall, elapsed, query_file):
        output_path = Path(output_dir)
        output_path.mkdir(parents=True, exist_ok=True)

        # Save summary
        summary = {
            "total_queries": overall.total_queries,
            "top_k": self.top_k,
            "precision": overall.avg_precision,
            "recall": overall.avg_recall,
            "hit_rate": overall.hit_rate,
            "f1": overall.avg_f1,
            "elapsed_time": elapsed,
            "metrics_at_k": overall.metrics_at_k,
        }
        with open(output_path / "summary.json", 'w', encoding='utf-8') as f:
            json.dump(summary, f, indent=2)

        # Save detailed results
        detailed = {
            "overall_metrics": asdict(overall),
            "query_metrics": [asdict(m) for m in query_metrics],
            "elapsed_time": elapsed,
        }
        with open(output_path / "evaluation_results.json", 'w', encoding='utf-8') as f:
            json.dump(detailed, f, indent=2, ensure_ascii=False)

        # Save config
        config = {
            "query_file": query_file,
            "top_k": self.top_k,
            "top_k_list": self.top_k_list,
            "embedding_model": self.searcher.model.model_name,
            "query_count": overall.total_queries,
        }
        with open(output_path / "config.json", 'w', encoding='utf-8') as f:
            json.dump(config, f, indent=2, ensure_ascii=False)

        logger.info(f"Results saved to: {output_dir}")
