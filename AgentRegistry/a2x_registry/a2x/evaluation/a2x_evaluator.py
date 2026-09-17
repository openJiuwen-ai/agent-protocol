"""A2X search evaluator.

Core evaluator class for measuring A2X taxonomy search performance.
Computes metrics: Precision, Recall, Hit Rate, F1, LLM usage stats.
"""

import json
import logging
import time
import threading
from pathlib import Path
from dataclasses import dataclass, asdict
from typing import List, Dict
from concurrent.futures import ThreadPoolExecutor, as_completed
from tqdm import tqdm

logger = logging.getLogger(__name__)

from a2x_registry.a2x.search.a2x_search import A2XSearch
from a2x_registry.a2x.evaluation.error_analysis import save_error_report
from a2x_registry.common.evaluation import compute_set_metrics


@dataclass
class QueryMetrics:
    """Metrics for a single query."""
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
    visited_categories: int
    pruned_categories: int
    expected_tools: List[str]
    found_tools: List[str]
    correct_tools: List[str]
    missed_tools: List[str]
    wrong_tools: List[str]
    visited_category_ids: List[str] = None  # Actual category IDs visited during search


@dataclass
class OverallMetrics:
    """Overall evaluation metrics."""
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
    avg_visited_categories: float
    avg_pruned_categories: float


class A2XEvaluator:
    """Evaluator for A2X search on ToolRet_middle."""

    def __init__(self, max_workers: int = 10,
                 taxonomy_path: str = None,
                 class_path: str = None,
                 service_path: str = None,
                 mode: str = "get_all"):
        """Initialize evaluator.

        Args:
            max_workers: Max parallel workers for query processing
            taxonomy_path: Custom taxonomy.json path (None = default ToolRet_middle)
            class_path: Custom class.json path (None = default ToolRet_middle)
            service_path: Custom service.json path (None = default ToolRet_middle)
            mode: Search mode ('get_all' or 'get_one')
        """
        self.max_workers = max_workers
        self.taxonomy_path = taxonomy_path
        self.class_path = class_path
        self.service_path = service_path
        self.mode = mode
        self.searcher = A2XSearch(
            taxonomy_path=taxonomy_path,
            class_path=class_path,
            service_path=service_path,
            max_workers=max_workers,
            parallel=True,
            mode=mode
        )

    def evaluate_single_query(self, query_obj: Dict) -> QueryMetrics:
        """Evaluate a single query.

        Args:
            query_obj: Query object from query.json

        Returns:
            QueryMetrics for this query
        """
        query_id = query_obj.get("id", "")
        query = query_obj.get("query", "")
        expected_tools = [t.get("id", "") for t in query_obj.get("correct_tools", [])]

        # Search
        try:
            results, stats = self.searcher.search(query)
            found_tools = [r.id for r in results]
        except Exception as e:
            logger.error(f"Error searching query {query_id}: {e}")
            found_tools = []
            stats = type('obj', (object,), {
                'llm_calls': 0,
                'total_tokens': 0,
                'visited_categories': [],
                'pruned_categories': [],
                'visited_category_ids': []
            })()

        # Compute metrics
        correct_tools, missed_tools, wrong_tools, precision, recall, hit = \
            compute_set_metrics(expected_tools, found_tools)

        return QueryMetrics(
            query_id=query_id,
            query=query,
            expected_count=len(expected_tools),
            found_count=len(found_tools),
            correct_count=len(correct_tools),
            precision=precision,
            recall=recall,
            hit=hit,
            llm_calls=stats.llm_calls,
            total_tokens=stats.total_tokens,
            visited_categories=len(stats.visited_categories),
            pruned_categories=len(stats.pruned_categories),
            expected_tools=expected_tools,
            found_tools=found_tools,
            correct_tools=correct_tools,
            missed_tools=missed_tools,
            wrong_tools=wrong_tools,
            visited_category_ids=stats.visited_category_ids
        )

    def _load_checkpoint(self, output_dir: str) -> tuple:
        """Load existing checkpoint if available.

        Returns:
            (completed_results: dict[int, QueryMetrics], completed_ids: set[str])
        """
        checkpoint_path = Path(output_dir) / "partial_results.jsonl"
        completed = {}  # idx -> QueryMetrics
        completed_ids = set()

        if not checkpoint_path.exists():
            return completed, completed_ids

        try:
            with open(checkpoint_path, 'r', encoding='utf-8') as f:
                for line in f:
                    line = line.strip()
                    if not line:
                        continue
                    data = json.loads(line)
                    idx = data.pop('_index')
                    metrics = QueryMetrics(**data)
                    completed[idx] = metrics
                    completed_ids.add(metrics.query_id)
            logger.info(f"Resumed from checkpoint: {len(completed)} queries already completed")
        except Exception as e:
            logger.warning(f"Failed to load checkpoint: {e}")
            return {}, set()

        return completed, completed_ids

    def _append_checkpoint(self, output_dir: str, idx: int, metrics: QueryMetrics,
                           checkpoint_lock: threading.Lock):
        """Append a single query result to the checkpoint file."""
        checkpoint_path = Path(output_dir) / "partial_results.jsonl"
        data = asdict(metrics)
        data['_index'] = idx
        with checkpoint_lock:
            with open(checkpoint_path, 'a', encoding='utf-8') as f:
                f.write(json.dumps(data, ensure_ascii=False) + '\n')

    def evaluate_batch(
        self,
        query_file: str,
        max_queries: int = None,
        output_dir: str = None,
        experiment_id: str = None,
        notes: str = ""
    ) -> tuple:
        """Evaluate on multiple queries with intermediate checkpointing.

        Args:
            query_file: Path to query.json file
            max_queries: Maximum number of queries to evaluate (None = all)
            output_dir: Directory to save results (also used for checkpoints)
            experiment_id: Experiment identifier for config.json
            notes: Additional notes for config.json

        Returns:
            (query_metrics, overall_metrics)
        """
        # Load queries
        with open(query_file, 'r', encoding='utf-8') as f:
            queries = json.load(f)

        if max_queries is not None:
            queries = queries[:max_queries]

        # Load checkpoint if exists
        completed_results = {}
        completed_ids = set()
        if output_dir:
            Path(output_dir).mkdir(parents=True, exist_ok=True)
            completed_results, completed_ids = self._load_checkpoint(output_dir)

        # Determine which queries still need evaluation
        pending_queries = [
            (i, q) for i, q in enumerate(queries)
            if q.get('id', '') not in completed_ids
        ]

        logger.info(f"Evaluating {len(queries)} queries ({len(completed_results)} from checkpoint, {len(pending_queries)} remaining)...")
        logger.info(f"Max workers: {self.max_workers}")

        # Evaluate queries in parallel
        start_time = time.time()
        checkpoint_lock = threading.Lock()

        def evaluate_with_index(idx: int, query_obj: Dict):
            """Evaluate and return with index."""
            return idx, self.evaluate_single_query(query_obj)

        # Start with checkpoint results
        results_with_idx = list(completed_results.items())
        total_completed = len(results_with_idx)

        with ThreadPoolExecutor(max_workers=self.max_workers) as executor:
            futures = {
                executor.submit(evaluate_with_index, i, q): i
                for i, q in pending_queries
            }

            with tqdm(total=len(queries), initial=total_completed,
                      desc="Evaluating queries", unit="query") as pbar:
                for future in as_completed(futures):
                    try:
                        idx, metrics = future.result()
                        results_with_idx.append((idx, metrics))
                        total_completed += 1
                        pbar.update(1)
                        pbar.set_postfix({
                            'hits': sum(1 for _, m in results_with_idx if m.hit),
                            'avg_prec': sum(m.precision for _, m in results_with_idx) / len(results_with_idx)
                        })
                        # Save checkpoint incrementally
                        if output_dir:
                            self._append_checkpoint(output_dir, idx, metrics, checkpoint_lock)
                    except Exception as e:
                        logger.error(f"Error in query evaluation: {e}")

        # Sort by original index
        results_with_idx.sort(key=lambda x: x[0])
        query_metrics = [m for _, m in results_with_idx]

        elapsed_time = time.time() - start_time

        # Compute and print overall metrics
        overall_metrics = self._compute_overall_metrics(query_metrics)
        self._print_results(overall_metrics, elapsed_time)

        # Save results
        if output_dir:
            self._save_results(output_dir, query_file, query_metrics,
                               overall_metrics, elapsed_time, experiment_id, notes)

        return query_metrics, overall_metrics

    def _compute_overall_metrics(self, query_metrics: List[QueryMetrics]) -> OverallMetrics:
        """Compute aggregate metrics from individual query results."""
        total_queries = len(query_metrics)
        total_llm_calls = sum(m.llm_calls for m in query_metrics)
        total_tokens = sum(m.total_tokens for m in query_metrics)

        avg_precision = sum(m.precision for m in query_metrics) / total_queries
        avg_recall = sum(m.recall for m in query_metrics) / total_queries
        hit_rate = sum(1 for m in query_metrics if m.hit) / total_queries

        if avg_precision + avg_recall > 0:
            avg_f1 = 2 * (avg_precision * avg_recall) / (avg_precision + avg_recall)
        else:
            avg_f1 = 0.0

        return OverallMetrics(
            total_queries=total_queries,
            avg_precision=avg_precision,
            avg_recall=avg_recall,
            hit_rate=hit_rate,
            avg_f1=avg_f1,
            total_llm_calls=total_llm_calls,
            total_tokens=total_tokens,
            avg_llm_calls=total_llm_calls / total_queries,
            avg_tokens=total_tokens / total_queries,
            avg_found_services=sum(m.found_count for m in query_metrics) / total_queries,
            avg_visited_categories=sum(m.visited_categories for m in query_metrics) / total_queries,
            avg_pruned_categories=sum(m.pruned_categories for m in query_metrics) / total_queries
        )

    @staticmethod
    def _print_results(overall: OverallMetrics, elapsed_time: float):
        """Print evaluation results to log."""
        logger.info("")
        logger.info("=" * 80)
        logger.info("Evaluation Results")
        logger.info("=" * 80)
        logger.info(f"Total Queries: {overall.total_queries}")
        logger.info(f"Time Elapsed: {elapsed_time:.2f}s")
        logger.info("")
        logger.info("Performance Metrics:")
        logger.info(f"  Precision: {overall.avg_precision:.4f}")
        logger.info(f"  Recall:    {overall.avg_recall:.4f}")
        logger.info(f"  Hit Rate:  {overall.hit_rate:.4f}")
        logger.info(f"  F1 Score:  {overall.avg_f1:.4f}")
        logger.info("")
        logger.info("LLM Usage:")
        logger.info(f"  Total LLM Calls: {overall.total_llm_calls}")
        logger.info(f"  Total Tokens:    {overall.total_tokens}")
        logger.info(f"  Avg LLM Calls:   {overall.avg_llm_calls:.2f}")
        logger.info(f"  Avg Tokens:      {overall.avg_tokens:.2f}")
        logger.info("")
        logger.info("Search Stats:")
        logger.info(f"  Avg Found Services:       {overall.avg_found_services:.2f}")
        logger.info(f"  Avg Visited Categories:   {overall.avg_visited_categories:.2f}")
        logger.info(f"  Avg Pruned Categories:    {overall.avg_pruned_categories:.2f}")
        logger.info("=" * 80)

    def _save_results(self, output_dir: str, query_file: str,
                      query_metrics: List[QueryMetrics], overall: OverallMetrics,
                      elapsed_time: float, experiment_id: str, notes: str):
        """Save all result files to output directory."""
        from datetime import datetime

        output_path = Path(output_dir)
        output_path.mkdir(parents=True, exist_ok=True)

        # config.json
        config = {
            "experiment_id": experiment_id or output_path.name,
            "date": datetime.now().strftime('%Y-%m-%d'),
            "query_file": query_file,
            "taxonomy_path": self.taxonomy_path,
            "class_path": self.class_path,
            "service_path": self.service_path,
            "search_config": {
                "parallel": True,
                "max_workers": self.max_workers,
                "mode": self.mode
            },
            "query_count": overall.total_queries,
            "notes": notes
        }
        with open(output_path / "config.json", 'w', encoding='utf-8') as f:
            json.dump(config, f, indent=2, ensure_ascii=False)

        # evaluation_results.json
        detailed = {
            "overall_metrics": asdict(overall),
            "query_metrics": [asdict(m) for m in query_metrics],
            "elapsed_time": elapsed_time
        }
        with open(output_path / "evaluation_results.json", 'w', encoding='utf-8') as f:
            json.dump(detailed, f, indent=2, ensure_ascii=False)

        # summary.json
        dataset_name = Path(self.service_path).parent.name if self.service_path else ""
        summary = {
            "dataset": dataset_name,
            "query_file": query_file,
            "mode": self.mode,
            "total_queries": overall.total_queries,
            "precision": overall.avg_precision,
            "recall": overall.avg_recall,
            "hit_rate": overall.hit_rate,
            "f1": overall.avg_f1,
            "avg_llm_calls": overall.avg_llm_calls,
            "avg_tokens": overall.avg_tokens,
            "elapsed_time": elapsed_time
        }
        with open(output_path / "summary.json", 'w', encoding='utf-8') as f:
            json.dump(summary, f, indent=2)

        logger.info(f"Results saved to: {output_dir}")

        # Error analysis report
        try:
            report_path = save_error_report(output_dir)
            logger.info(f"Error analysis saved to: {report_path}")
        except Exception as e:
            logger.warning(f"Failed to generate error report: {e}")


