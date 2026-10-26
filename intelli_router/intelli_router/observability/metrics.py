"""指标采集事件处理器"""
import logging
import re
import threading
import time
from collections import defaultdict, deque
from dataclasses import dataclass, field
from typing import TYPE_CHECKING, Dict, Any, Iterable, List, Deque, Optional

from .events import RoutingEvent, RoutingEventType
from .handler import EventHandler

logger = logging.getLogger(__name__)

if TYPE_CHECKING:
    from prometheus_client import CollectorRegistry

# Prometheus 完整指标名规范: [a-zA-Z_:][a-zA-Z0-9_:]*
# 此正则仅约束 prefix 本身（禁止冒号是刻意的，冒号保留给特定命名惯例）；
# 完整指标名 = prefix + 本模块固定的合法后缀（如 _requests_total），
# 整体合规由拼接处保证
_PROMETHEUS_NAME_RE = re.compile(r"^[a-zA-Z_][a-zA-Z0-9_]*$")
# prefix 最大长度（拼接指标名后总长不超过 Prometheus 规范上限）
_PROMETHEUS_PREFIX_MAX_LEN = 256


@dataclass
class LatencyStats:
    """简易延迟统计（含分位数）"""

    _SAMPLE_LIMIT: int = field(default=1000, repr=False)

    count: int = 0
    total: float = 0.0
    min_val: float = float("inf")
    max_val: float = 0.0
    _samples: List[float] = field(default_factory=list, repr=False)

    def observe(self, value: float) -> None:
        self.count += 1
        self.total += value
        if value < self.min_val:
            self.min_val = value
        if value > self.max_val:
            self.max_val = value
        self._samples.append(value)
        if len(self._samples) > self._SAMPLE_LIMIT:
            self._samples = self._samples[-self._SAMPLE_LIMIT:]

    def percentile(self, p: float) -> Optional[float]:
        """计算分位数 (0-100)"""
        if not self._samples:
            return None
        sorted_s = sorted(self._samples)
        idx = int(len(sorted_s) * p / 100)
        return sorted_s[min(idx, len(sorted_s) - 1)]

    def snapshot(self) -> "LatencyStats":
        """返回当前统计的独立副本（样本列表拷贝为新 list）。

        供 get_stats 在持锁期间廉价地取快照，然后在锁外计算
        分位数等派生值，避免与并发 observe() 竞争样本列表。
        """
        return LatencyStats(
            count=self.count,
            total=self.total,
            min_val=self.min_val,
            max_val=self.max_val,
            _samples=list(self._samples),
        )

    @property
    def avg(self) -> float:
        return self.total / self.count if self.count > 0 else 0.0

    def to_dict(self) -> Dict[str, Any]:
        if self.count == 0:
            return {"count": 0, "avg": 0.0, "min": None, "max": None,
                    "p50": None, "p95": None, "p99": None}
        return {
            "count": self.count,
            "avg": round(self.avg, 4),
            "min": round(self.min_val, 4),
            "max": round(self.max_val, 4),
            "p50": round(self.percentile(50), 4),
            "p95": round(self.percentile(95), 4),
            "p99": round(self.percentile(99), 4),
        }


class MetricsCollector(EventHandler):
    """指标采集器

    维护内存中的请求统计数据，可通过 get_stats() 获取快照。
    可选开启 Prometheus 指标导出（需安装 prometheus-client）。

    用法:
        collector = MetricsCollector()
        bus.register(collector)
        # ... 执行请求 ...
        print(collector.get_stats())

        # 可选 Prometheus:
        collector = MetricsCollector(enable_prometheus=True)

        # 同进程需要多个 collector 时，各自传入独立的 registry，
        # 避免全局默认 registry 的 DuplicateTimeseries:
        from prometheus_client import CollectorRegistry
        collector = MetricsCollector(
            enable_prometheus=True,
            prometheus_registry=CollectorRegistry(),
        )
    """

    def __init__(
        self,
        enable_prometheus: bool = False,
        prometheus_prefix: str = "intelli_router",
        prometheus_registry: Optional["CollectorRegistry"] = None,
    ):
        if not isinstance(prometheus_prefix, str):
            raise TypeError(
                f"prometheus_prefix must be a str, got {type(prometheus_prefix).__name__}"
            )
        if not _PROMETHEUS_NAME_RE.match(prometheus_prefix):
            raise ValueError(
                "prometheus_prefix must match [a-zA-Z_][a-zA-Z0-9_]* and be non-empty, "
                f"got {prometheus_prefix!r}"
            )
        if len(prometheus_prefix) > _PROMETHEUS_PREFIX_MAX_LEN:
            raise ValueError(
                f"prometheus_prefix length must be <= {_PROMETHEUS_PREFIX_MAX_LEN}, "
                f"got {len(prometheus_prefix)}"
            )
        self._enable_prometheus = enable_prometheus
        self._prefix = prometheus_prefix
        # Prometheus registry：None = 全局默认 registry（向后兼容）；
        # 显式传入 CollectorRegistry 时所有 Counter/Histogram 注册到
        # 该 registry，同进程可并存多个 collector 而不触发
        # DuplicateTimeseries（指标名只由 prefix 决定，同一 prefix
        # 重复注册进全局 registry 会冲突）
        self._registry = prometheus_registry

        # 统计数据互斥锁：handle_event 记账（请求协程并发调用）与
        # get_stats/reset（如 ThreadingHTTPServer 的 /api/stats 线程）
        # 并发访问同一批 dict/deque，须互斥以防
        # "dictionary changed size during iteration"
        self._lock = threading.Lock()

        # 内存计数器
        self._request_count: int = 0
        self._success_count: int = 0
        self._failure_count: int = 0
        self._retry_count: int = 0
        self._exhausted_count: int = 0

        # 按 model 统计
        self._model_requests: Dict[str, int] = defaultdict(int)
        self._model_successes: Dict[str, int] = defaultdict(int)
        self._model_failures: Dict[str, int] = defaultdict(int)

        # 按 deployment 细分统计
        self._deployment_requests: Dict[str, int] = defaultdict(int)
        self._deployment_successes: Dict[str, int] = defaultdict(int)
        self._deployment_failures: Dict[str, int] = defaultdict(int)
        self._deployment_latency: Dict[str, LatencyStats] = defaultdict(LatencyStats)
        self._deployment_tokens: Dict[str, int] = defaultdict(int)
        self._deployment_provider: Dict[str, str] = {}

        # 按错误类型统计
        self._errors_by_type: Dict[str, int] = defaultdict(int)

        # 延迟统计
        self._latency: LatencyStats = LatencyStats()
        self._latency_by_model: Dict[str, LatencyStats] = defaultdict(LatencyStats)

        # Token 统计
        self._total_prompt_tokens: int = 0
        self._total_completion_tokens: int = 0

        # Stream 统计
        self._stream_count: int = 0
        self._total_chunks: int = 0
        # 流式请求被消费者提前中断（STREAM_CANCELLED）的终态计数。
        # 成功/失败/取消三类终态与 total（STARTED 时已计）配对：
        # successful + failed + cancelled + 在途 == total_requests
        # （在途为派生值，不单独存储）。
        self._cancelled_count: int = 0

        # TTFT (Time To First Token) 统计
        self._ttft: LatencyStats = LatencyStats()
        self._ttft_by_model: Dict[str, LatencyStats] = defaultdict(LatencyStats)

        # 吐字速率 (tokens/s)：仅统计非流式请求的真实 token 速率
        self._tokens_per_sec: LatencyStats = LatencyStats()
        # 出块速率 (chunks/s)：流式请求的 chunk 速率（chunk ≠ token，不可混入 tokens_per_sec）
        self._chunks_per_sec: LatencyStats = LatencyStats()

        # QPS 追踪（最近 60s 请求时间戳）
        self._request_timestamps: Deque[float] = deque()

        # 请求时间线（最近 N 条记录）
        self._timeline: Deque[Dict[str, Any]] = deque(maxlen=100)

        # Prometheus (延迟初始化)
        self._prom = None
        # expose_prometheus 启动的 HTTP 服务句柄（延迟赋值）
        self._prom_server = None
        self._prom_thread = None
        if enable_prometheus:
            self._init_prometheus()

    def _init_prometheus(self) -> None:
        try:
            from prometheus_client import REGISTRY, Counter, Histogram
        except ImportError:
            raise ImportError(
                "prometheus-client is required for Prometheus metrics. "
                "Install with: pip install intelli-router[metrics]"
            )

        p = self._prefix
        # 显式传入 registry 时注册到该 registry；None 时注册到全局默认
        # REGISTRY（与旧行为一致）。注意不能把 None 直接透传给
        # prometheus_client——显式 registry=None 在其语义里是"不注册"。
        registry = self._registry if self._registry is not None else REGISTRY
        self._prom = {
            "requests_total": Counter(
                f"{p}_requests_total",
                "Total routing requests",
                ["model", "status"],
                registry=registry,
            ),
            "latency": Histogram(
                f"{p}_request_latency_seconds",
                "Request latency in seconds",
                ["model"],
                buckets=(0.1, 0.25, 0.5, 1.0, 2.5, 5.0, 10.0, 30.0, 60.0),
                registry=registry,
            ),
            "tokens_total": Counter(
                f"{p}_tokens_total",
                "Total tokens consumed",
                ["model", "type"],
                registry=registry,
            ),
            "retries_total": Counter(
                f"{p}_retries_total",
                "Total retry attempts",
                ["model"],
                registry=registry,
            ),
            "errors_total": Counter(
                f"{p}_errors_total",
                "Total errors by type",
                ["model", "error_type"],
                registry=registry,
            ),
        }

    async def handle_event(self, event: RoutingEvent) -> None:
        """处理事件并更新指标（线程安全）"""
        et = event.event_type

        # 记账段落整体持锁：本方法作为 EventBus handler 会被多个请求
        # 协程并发调用，同时 get_stats/reset 可能被 HTTP 服务线程调用，
        # 无锁并发读写统计字典会抛
        # "RuntimeError: dictionary changed size during iteration"
        with self._lock:
            if et == RoutingEventType.REQUEST_STARTED:
                self._on_request_started(event)
            elif et == RoutingEventType.REQUEST_SUCCEEDED:
                self._on_request_succeeded(event)
            elif et == RoutingEventType.REQUEST_RETRIED:
                self._on_request_retried(event)
            elif et == RoutingEventType.ALL_DEPLOYMENTS_EXHAUSTED:
                self._on_all_exhausted(event)
            elif et == RoutingEventType.STREAM_STARTED:
                self._on_stream_started(event)
            elif et == RoutingEventType.STREAM_SUCCEEDED:
                self._on_stream_succeeded(event)
            elif et == RoutingEventType.STREAM_CANCELLED:
                self._on_stream_cancelled(event)
            elif et == RoutingEventType.DEPLOYMENTS_UPDATED:
                self._on_deployments_updated(event)

    def _on_request_started(self, event: RoutingEvent) -> None:
        self._request_count += 1
        self._model_requests[event.model] += 1
        self._request_timestamps.append(event.timestamp)

    def _on_request_succeeded(self, event: RoutingEvent) -> None:
        self._success_count += 1
        self._model_successes[event.model] += 1
        if event.deployment_id:
            self._deployment_requests[event.deployment_id] += 1
            self._deployment_successes[event.deployment_id] += 1
            if event.provider:
                self._deployment_provider[event.deployment_id] = event.provider
            if event.latency is not None:
                self._deployment_latency[event.deployment_id].observe(event.latency)
            total_tok = (event.prompt_tokens or 0) + (event.completion_tokens or 0)
            if total_tok:
                self._deployment_tokens[event.deployment_id] += total_tok
        if event.latency is not None:
            self._latency.observe(event.latency)
            self._latency_by_model[event.model].observe(event.latency)
        if event.prompt_tokens:
            self._total_prompt_tokens += event.prompt_tokens
        if event.completion_tokens:
            self._total_completion_tokens += event.completion_tokens
        # 吐字速率
        if event.completion_tokens and event.latency and event.latency > 0:
            self._tokens_per_sec.observe(event.completion_tokens / event.latency)

        # 时间线
        self._timeline.append({
            "ts": event.timestamp,
            "type": "success",
            "model": event.model,
            "deployment": event.deployment_id,
            "latency": event.latency,
            "tokens": (event.prompt_tokens or 0) + (event.completion_tokens or 0),
        })

        if self._prom:
            self._prom["requests_total"].labels(model=event.model, status="success").inc()
            if event.latency is not None:
                self._prom["latency"].labels(model=event.model).observe(event.latency)
            if event.prompt_tokens:
                self._prom["tokens_total"].labels(model=event.model, type="prompt").inc(event.prompt_tokens)
            if event.completion_tokens:
                self._prom["tokens_total"].labels(model=event.model, type="completion").inc(event.completion_tokens)

    def _on_request_retried(self, event: RoutingEvent) -> None:
        self._retry_count += 1
        if event.deployment_id:
            # RETRIED 事件携带失败部署的 deployment_id（REQUEST_STARTED/
            # EXHAUSTED 均不携带），在此把这次"尝试"计入该部署的
            # requests，使其保持"该部署被尝试次数"语义：
            # requests = successes + failures。
            # 注意：全局 failed 口径不变——最终失败仍只在
            # _on_all_exhausted 累加，重试不算失败。
            self._deployment_requests[event.deployment_id] += 1
            self._deployment_failures[event.deployment_id] += 1
        if event.error_type:
            self._errors_by_type[event.error_type] += 1
        self._timeline.append({
            "ts": event.timestamp,
            "type": "retry",
            "model": event.model,
            "deployment": event.deployment_id,
            "latency": None,
            "error": event.error_type,
        })
        if self._prom:
            self._prom["retries_total"].labels(model=event.model).inc()
            if event.error_type:
                self._prom["errors_total"].labels(model=event.model, error_type=event.error_type).inc()

    def _on_all_exhausted(self, event: RoutingEvent) -> None:
        self._exhausted_count += 1
        # exhausted 即请求最终失败（重试后仍无可用部署），计入 failed
        self._failure_count += 1
        self._model_failures[event.model] += 1
        if self._prom:
            self._prom["requests_total"].labels(model=event.model, status="exhausted").inc()

    def _on_stream_started(self, event: RoutingEvent) -> None:
        self._stream_count += 1
        self._request_count += 1
        self._model_requests[event.model] += 1
        self._request_timestamps.append(event.timestamp)

    def _on_stream_succeeded(self, event: RoutingEvent) -> None:
        self._success_count += 1
        self._model_successes[event.model] += 1
        if event.deployment_id:
            self._deployment_requests[event.deployment_id] += 1
            self._deployment_successes[event.deployment_id] += 1
            if event.provider:
                self._deployment_provider[event.deployment_id] = event.provider
            if event.latency is not None:
                self._deployment_latency[event.deployment_id].observe(event.latency)
            # router 从流式 usage 中提取的真实 token 数（未携带时为
            # None/0，不统计），补齐此前流式输出完全不计 token 的缺口
            total_tok = (event.prompt_tokens or 0) + (event.completion_tokens or 0)
            if total_tok:
                self._deployment_tokens[event.deployment_id] += total_tok
        if event.latency is not None:
            self._latency.observe(event.latency)
            self._latency_by_model[event.model].observe(event.latency)
        if event.prompt_tokens:
            self._total_prompt_tokens += event.prompt_tokens
        if event.completion_tokens:
            self._total_completion_tokens += event.completion_tokens
        # 真实 token 的吐字速率（与 !315 意见14 的约束不冲突：该约束
        # 禁止的是把 chunk 数冒充 token，这里是 usage 上报的真值）
        if event.completion_tokens and event.latency and event.latency > 0:
            self._tokens_per_sec.observe(event.completion_tokens / event.latency)
        if event.chunk_count:
            self._total_chunks += event.chunk_count
        # 出块速率（chunk ≠ token，独立统计，不污染 tokens_per_sec）
        if event.chunk_count and event.latency and event.latency > 0:
            self._chunks_per_sec.observe(event.chunk_count / event.latency)
        # TTFT
        ttft = event.extra.get("ttft")
        if ttft is not None:
            self._ttft.observe(ttft)
            self._ttft_by_model[event.model].observe(ttft)
        # 时间线
        self._timeline.append({
            "ts": event.timestamp,
            "type": "stream_success",
            "model": event.model,
            "deployment": event.deployment_id,
            "latency": event.latency,
            "chunks": event.chunk_count,
            "ttft": ttft,
        })
        if self._prom:
            self._prom["requests_total"].labels(model=event.model, status="success").inc()
            if event.latency is not None:
                self._prom["latency"].labels(model=event.model).observe(event.latency)

    def _on_stream_cancelled(self, event: RoutingEvent) -> None:
        """流被消费者提前中断（break/aclose/取消）：终态之一。

        只计 cancelled 计数器：不加 successful/failed，不动 total
        （STARTED 时已计），也不 observe 延迟/token——中断流的
        partial 延迟不属于完整请求延迟，混入会拉低 avg latency；
        by_deployment 口径（requests = successes + failures）同样
        不纳入取消。时间线记一条 stream_cancelled 供追溯（可携带
        reason/latency/chunk_count）。
        """
        self._cancelled_count += 1
        entry = {
            "ts": event.timestamp,
            "type": "stream_cancelled",
            "model": event.model,
            "deployment": event.deployment_id,
            "latency": event.latency,
            "chunks": event.chunk_count,
        }
        reason = event.extra.get("reason")
        if reason is not None:
            entry["reason"] = reason
        self._timeline.append(entry)

    def _on_deployments_updated(self, event: RoutingEvent) -> None:
        """热替换部署：清理已下线部署的统计键（意见：热替换后 metrics
        残留旧键，看板 Deployments 表格无法区分在线与历史）。

        纯键清理：不影响任何计数器（total/successful/failed/streams/
        cancelled 等都不动——历史请求的计数已发生在过去，删键只是让
        by_deployment 不再展示下线部署）。removed_ids 缺失或非 list
        时防御性忽略并打 debug 日志。
        """
        removed_ids = event.extra.get("removed_ids")
        if not isinstance(removed_ids, list):
            logger.debug(
                "DEPLOYMENTS_UPDATED 事件未携带有效的 removed_ids 列表"
                "（got %r），跳过部署统计键清理",
                removed_ids,
            )
            return
        self._drop_deployment_keys(removed_ids)

    def _drop_deployment_keys(self, deployment_ids: Iterable[str]) -> None:
        """从全部 _deployment_* 字典中删除给定部署的键（调用方须已持锁）。

        LatencyStats 对象随 _deployment_latency 的键一并删除，
        不需要单独清理。
        """
        for dep_id in deployment_ids:
            self._deployment_requests.pop(dep_id, None)
            self._deployment_successes.pop(dep_id, None)
            self._deployment_failures.pop(dep_id, None)
            self._deployment_latency.pop(dep_id, None)
            self._deployment_tokens.pop(dep_id, None)
            self._deployment_provider.pop(dep_id, None)

    def remove_deployment(self, deployment_id: str) -> None:
        """删除单个部署的全部统计键（线程安全）。

        供不走 event_bus（或同步上下文）的宿主在部署下线时调用，
        与 DEPLOYMENTS_UPDATED 事件路径等效。不存在的 id 静默忽略。
        """
        with self._lock:
            self._drop_deployment_keys([deployment_id])

    def prune_deployments(self, keep_ids: Iterable[str]) -> None:
        """清理不在 keep_ids 内的全部部署统计键（线程安全）。

        keep_ids 为保留集合（当前在线部署 id 的任意可迭代），
        不在集合内的部署键从所有 _deployment_* 字典中清除。
        适合全量同步场景：router.update_deployments 热替换后用
        新列表全集调用，一次性清掉所有已下线部署的残留。
        """
        keep = set(keep_ids)
        with self._lock:
            stale = [
                dep_id
                for dep_id in (
                    self._deployment_requests.keys()
                    | self._deployment_successes.keys()
                    | self._deployment_failures.keys()
                    | self._deployment_latency.keys()
                    | self._deployment_tokens.keys()
                    | self._deployment_provider.keys()
                )
                if dep_id not in keep
            ]
            self._drop_deployment_keys(stale)

    def get_stats(self) -> Dict[str, Any]:
        """返回当前所有指标快照（线程安全）

        持锁期间只做轻量拷贝（含 QPS 时间窗裁剪，它本身也是一次
        记账变更），分位数计算等重活在锁外基于快照进行，避免
        长时间持锁阻塞事件记账。
        """
        with self._lock:
            # 计算 QPS（最近 60s）
            now = time.time()
            cutoff = now - 60
            while self._request_timestamps and self._request_timestamps[0] < cutoff:
                self._request_timestamps.popleft()
            qps = len(self._request_timestamps) / 60.0

            # 局部快照：锁内只拷贝，不做派生计算
            snap = {
                "total_requests": self._request_count,
                "successful": self._success_count,
                "failed": self._failure_count,
                "cancelled": self._cancelled_count,
                "retries": self._retry_count,
                "exhausted": self._exhausted_count,
                "streams": self._stream_count,
                "total_chunks": self._total_chunks,
                "tokens_prompt": self._total_prompt_tokens,
                "tokens_completion": self._total_completion_tokens,
                "model_requests": dict(self._model_requests),
                "model_successes": dict(self._model_successes),
                "model_failures": dict(self._model_failures),
                "deployment_requests": dict(self._deployment_requests),
                "deployment_successes": dict(self._deployment_successes),
                "deployment_failures": dict(self._deployment_failures),
                "deployment_latency": {
                    k: v.snapshot() for k, v in self._deployment_latency.items()
                },
                "deployment_tokens": dict(self._deployment_tokens),
                "deployment_provider": dict(self._deployment_provider),
                "errors_by_type": dict(self._errors_by_type),
                "latency": self._latency.snapshot(),
                "latency_by_model": {
                    k: v.snapshot() for k, v in self._latency_by_model.items()
                },
                "ttft": self._ttft.snapshot(),
                "ttft_by_model": {
                    k: v.snapshot() for k, v in self._ttft_by_model.items()
                },
                "tokens_per_sec": self._tokens_per_sec.snapshot(),
                "chunks_per_sec": self._chunks_per_sec.snapshot(),
                "timeline": list(self._timeline),
            }

        return {
            "total_requests": snap["total_requests"],
            "successful": snap["successful"],
            "failed": snap["failed"],
            "cancelled": snap["cancelled"],
            "retries": snap["retries"],
            "exhausted": snap["exhausted"],
            "streams": snap["streams"],
            "total_chunks": snap["total_chunks"],
            "qps": round(qps, 2),
            "tokens_per_sec": snap["tokens_per_sec"].to_dict(),
            "chunks_per_sec": snap["chunks_per_sec"].to_dict(),
            "latency": snap["latency"].to_dict(),
            "ttft": snap["ttft"].to_dict(),
            "tokens": {
                "prompt": snap["tokens_prompt"],
                "completion": snap["tokens_completion"],
                "total": snap["tokens_prompt"] + snap["tokens_completion"],
            },
            "by_model": {
                model: {
                    "requests": snap["model_requests"].get(model, 0),
                    "successes": snap["model_successes"].get(model, 0),
                    "failures": snap["model_failures"].get(model, 0),
                    "latency": snap["latency_by_model"][model].to_dict()
                    if model in snap["latency_by_model"] else None,
                    "ttft": snap["ttft_by_model"][model].to_dict()
                    if model in snap["ttft_by_model"] else None,
                }
                # 并集：有请求或有失败记录的 model 都应出现
                # （正常路径下失败均有配对的 STARTED 事件；取并集是防御旧版本
                # 事件流或外部直发事件时的 key 缺失）
                for model in snap["model_requests"].keys()
                | snap["model_failures"].keys()
            },
            "by_deployment": {
                dep_id: {
                    "provider": snap["deployment_provider"].get(dep_id, ""),
                    "requests": snap["deployment_requests"].get(dep_id, 0),
                    "successes": snap["deployment_successes"].get(dep_id, 0),
                    "failures": snap["deployment_failures"].get(dep_id, 0),
                    "latency": snap["deployment_latency"][dep_id].to_dict()
                    if dep_id in snap["deployment_latency"] else None,
                    "tokens": snap["deployment_tokens"].get(dep_id, 0),
                }
                # 并集：有尝试（requests，成功或重试路径累计）或有失败
                # 记录的部署都应出现（与 by_model 同口径）。此前只遍历
                # _deployment_requests（仅成功回调写入），只失败未成功的
                # 部署会整条丢失——d1 失败 d2 成功时 by_deployment 只剩
                # d2，requests 语义塌缩成"成功数"。
                # requests 一律用 .get 读取，避免 defaultdict 在快照
                # 构建时反向插入污染统计字典。
                for dep_id in snap["deployment_requests"].keys()
                | snap["deployment_failures"].keys()
            },
            "errors_by_type": snap["errors_by_type"],
            "timeline": snap["timeline"],
        }

    def expose_prometheus(
        self, port: int = 9090, addr: str = "127.0.0.1"
    ) -> tuple:
        """启动 Prometheus HTTP 服务，暴露指标供 Grafana 等 scrape

        Args:
            port: HTTP 服务端口，默认 9090
            addr: 绑定地址，默认 127.0.0.1（仅本机访问；
                对外暴露需显式传入如 "0.0.0.0"）

        Returns:
            (server, thread) 元组（prometheus_client.start_http_server
            的返回值），调用方可用其 shutdown()/server_close() 停止服务。

        用法:
            collector = MetricsCollector(enable_prometheus=True)
            server, thread = collector.expose_prometheus(port=9090)
            # 然后 curl http://localhost:9090/metrics
        """
        if not self._enable_prometheus:
            self._init_prometheus()
            self._enable_prometheus = True

        try:
            from prometheus_client import REGISTRY, start_http_server
        except ImportError:
            raise ImportError(
                "prometheus-client is required for Prometheus metrics. "
                "Install with: pip install intelli-router[metrics]"
            )

        # 保留句柄，供调用方（或后续 stop_prometheus）停止服务。
        # HTTP 服务挂在 collector 自己的 registry 上（显式传入的独立
        # registry，或全局默认 registry），否则自定义 registry 的
        # collector 暴露的会是空的全局 registry。
        registry = self._registry if self._registry is not None else REGISTRY
        self._prom_server, self._prom_thread = start_http_server(
            port, addr=addr, registry=registry
        )
        return self._prom_server, self._prom_thread

    def reset(self) -> None:
        """重置所有内存指标（线程安全，与记账/读取互斥）"""
        with self._lock:
            self._request_count = 0
            self._success_count = 0
            self._failure_count = 0
            self._retry_count = 0
            self._exhausted_count = 0
            self._model_requests.clear()
            self._model_successes.clear()
            self._model_failures.clear()
            self._deployment_requests.clear()
            self._deployment_successes.clear()
            self._deployment_failures.clear()
            self._deployment_latency.clear()
            self._deployment_tokens.clear()
            self._deployment_provider.clear()
            self._errors_by_type.clear()
            self._latency = LatencyStats()
            self._latency_by_model.clear()
            self._total_prompt_tokens = 0
            self._total_completion_tokens = 0
            self._stream_count = 0
            self._total_chunks = 0
            self._cancelled_count = 0
            self._ttft = LatencyStats()
            self._ttft_by_model.clear()
            self._tokens_per_sec = LatencyStats()
            self._chunks_per_sec = LatencyStats()
            self._request_timestamps.clear()
            self._timeline.clear()
