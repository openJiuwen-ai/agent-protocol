"""Traditional (MCP-style) search evaluator.

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
from concurrent.futures import ThreadPoolExecutor, as_completed
from tqdm import tqdm

logger = logging.getLogger(__name__)

from a2x_registry.traditional.search.traditional_search import TraditionalSearch
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
    llm_calls: int
    total_tokens: int
    expected_tools: List[str]
    found_tools: List[str]
    correct_tools: List[str]
    missed_tools: List[str]
    wrong_tools: List[str]


@dataclass
class OverallMetrics:
    """Overall evaluation metrics, matching A2X OverallMetrics interface."""
    total_queries: int
    avg_precision: float
    avg_recall: float
    hit_rate: float
    avg_f1: float
    total_llm_calls: int
    total_tokens: int
    avg_llm_calls: float
    avg_tokens: float
    avg_found_services: float


class TraditionalEvaluator:
    """Evaluator for Traditional full-context search.

    Interface consistent with A2XEvaluator.

    Args:
        service_path: Path to service.json
        max_workers: Number of parallel evaluation workers
    """

    def __init__(
        self,
        service_path: str = "database/ToolRet_clean/service.json",
        max_workers: int = 10,
        **kwargs,
    ):
        self.max_workers = max_workers
        self.searcher = TraditionalSearch(service_path=service_path)

    def evaluate_single_query(self, query_obj: Dict) -> QueryMetrics:
        """Evaluate a single query."""
        query_id = query_obj.get("id", "")
        query = query_obj.get("query", "")
        expected_tools = [t.get("id", "") for t in query_obj.get("correct_tools", [])]

        try:
            results, stats = self.searcher.search(query)
            found_tools = [r.id for r in results]
        except Exception as e:
            logger.error(f"Error searching query {query_id}: {e}")
            found_tools = []
            stats = type('obj', (object,), {'llm_calls': 0, 'total_tokens': 0})()

        correct, missed, wrong, precision, recall, hit = \
            compute_set_metrics(expected_tools, found_tools)

        return QueryMetrics(
            query_id=query_id,
            query=query,
            expected_count=len(expected_tools),
            found_count=len(found_tools),
            correct_count=len(correct),
            precision=precision,
            recall=recall,
            hit=hit,
            llm_calls=stats.llm_calls,
            total_tokens=stats.total_tokens,
            expected_tools=expected_tools,
            found_tools=found_tools,
            correct_tools=correct,
            missed_tools=missed,
            wrong_tools=wrong,
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

        logger.info("=== Traditional (MCP-style) Evaluation ===")
        logger.info(f"Services: {len(self.searcher.services)}")
        logger.info(f"Queries: {len(queries)}")
        logger.info(f"Workers: {self.max_workers}")

        start_time = time.time()

        def evaluate_with_index(idx, query_obj):
            return idx, self.evaluate_single_query(query_obj)

        results_with_idx = []
        with ThreadPoolExecutor(max_workers=self.max_workers) as executor:
            futures = {
                executor.submit(evaluate_with_index, i, q): i
                for i, q in enumerate(queries)
            }

            with tqdm(total=len(queries), desc="Evaluating", unit="query") as pbar:
                for future in as_completed(futures):
                    try:
                        idx, metrics = future.result()
                        results_with_idx.append((idx, metrics))
                        pbar.update(1)
                        pbar.set_postfix({
                            'recall': f"{sum(m.recall for _, m in results_with_idx) / len(results_with_idx):.2%}",
                            'hits': sum(1 for _, m in results_with_idx if m.hit),
                        })
                    except Exception as e:
                        logger.error(f"Error: {e}")

        results_with_idx.sort(key=lambda x: x[0])
        query_metrics = [m for _, m in results_with_idx]

        elapsed = time.time() - start_time

        # Compute overall metrics
        n = len(query_metrics)
        total_llm_calls = sum(m.llm_calls for m in query_metrics)
        total_tokens = sum(m.total_tokens for m in query_metrics)
        avg_precision = sum(m.precision for m in query_metrics) / n
        avg_recall = sum(m.recall for m in query_metrics) / n
        hit_rate = sum(1 for m in query_metrics if m.hit) / n
        avg_f1 = (2 * avg_precision * avg_recall / (avg_precision + avg_recall)
                  if avg_precision + avg_recall > 0 else 0.0)

        overall = OverallMetrics(
            total_queries=n,
            avg_precision=avg_precision,
            avg_recall=avg_recall,
            hit_rate=hit_rate,
            avg_f1=avg_f1,
            total_llm_calls=total_llm_calls,
            total_tokens=total_tokens,
            avg_llm_calls=total_llm_calls / n,
            avg_tokens=total_tokens / n,
            avg_found_services=sum(m.found_count for m in query_metrics) / n,
        )

        self._print_results(overall, elapsed)

        if output_dir:
            self._save_results(output_dir, query_metrics, overall, elapsed, query_file)

        return query_metrics, overall

    def _print_results(self, overall: OverallMetrics, elapsed: float):
        logger.info("")
        logger.info("=" * 80)
        logger.info("Traditional (MCP-style) Evaluation Results")
        logger.info("=" * 80)
        logger.info(f"Total Queries: {overall.total_queries}")
        logger.info(f"Time Elapsed: {elapsed:.2f}s")
        logger.info("")
        logger.info("Performance Metrics:")
        logger.info(f"  Precision: {overall.avg_precision:.4f}")
        logger.info(f"  Recall:    {overall.avg_recall:.4f}")
        logger.info(f"  Hit Rate:  {overall.hit_rate:.4f}")
        logger.info(f"  F1 Score:  {overall.avg_f1:.4f}")
        logger.info("")
        logger.info("LLM Usage:")
        logger.info(f"  Avg LLM Calls: {overall.avg_llm_calls:.2f}")
        logger.info(f"  Avg Tokens/Query: {overall.avg_tokens:.0f}")
        logger.info("")
        logger.info("Search Stats:")
        logger.info(f"  Avg Found Services: {overall.avg_found_services:.2f}")
        logger.info("=" * 80)

    def _save_results(self, output_dir, query_metrics, overall, elapsed, query_file):
        output_path = Path(output_dir)
        output_path.mkdir(parents=True, exist_ok=True)

        summary = {
            "method": "traditional",
            "total_queries": overall.total_queries,
            "precision": overall.avg_precision,
            "recall": overall.avg_recall,
            "hit_rate": overall.hit_rate,
            "f1": overall.avg_f1,
            "avg_llm_calls": overall.avg_llm_calls,
            "avg_tokens": overall.avg_tokens,
            "avg_found_services": overall.avg_found_services,
            "elapsed_time": elapsed,
        }
        with open(output_path / "summary.json", 'w', encoding='utf-8') as f:
            json.dump(summary, f, indent=2)

        detailed = {
            "overall_metrics": asdict(overall),
            "query_metrics": [asdict(m) for m in query_metrics],
            "elapsed_time": elapsed,
        }
        with open(output_path / "evaluation_results.json", 'w', encoding='utf-8') as f:
            json.dump(detailed, f, indent=2, ensure_ascii=False)

        config = {
            "method": "traditional",
            "query_file": query_file,
            "service_count": len(self.searcher.services),
            "query_count": overall.total_queries,
        }
        with open(output_path / "config.json", 'w', encoding='utf-8') as f:
            json.dump(config, f, indent=2, ensure_ascii=False)

        logger.info(f"Results saved to: {output_dir}")
