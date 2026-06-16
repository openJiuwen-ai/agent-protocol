"""指标采集事件处理器"""
import time
from collections import defaultdict, deque
from dataclasses import dataclass, field
from typing import Dict, Any, List, Deque, Optional

from .events import RoutingEvent, RoutingEventType
from .handler import EventHandler


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
    """

    def __init__(
        self,
        enable_prometheus: bool = False,
        prometheus_prefix: str = "intelli_router",
    ):
        self._enable_prometheus = enable_prometheus
        self._prefix = prometheus_prefix

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

        # TTFT (Time To First Token) 统计
        self._ttft: LatencyStats = LatencyStats()
        self._ttft_by_model: Dict[str, LatencyStats] = defaultdict(LatencyStats)

        # 吐字速率 (tokens/s)
        self._tokens_per_sec: LatencyStats = LatencyStats()

        # QPS 追踪（最近 60s 请求时间戳）
        self._request_timestamps: Deque[float] = deque()

        # 请求时间线（最近 N 条记录）
        self._timeline: Deque[Dict[str, Any]] = deque(maxlen=100)

        # Prometheus (延迟初始化)
        self._prom = None
        if enable_prometheus:
            self._init_prometheus()

    def _init_prometheus(self) -> None:
        try:
            from prometheus_client import Counter, Histogram
        except ImportError:
            raise ImportError(
                "prometheus-client is required for Prometheus metrics. "
                "Install with: pip install intelli-router[metrics]"
            )

        p = self._prefix
        self._prom = {
            "requests_total": Counter(
                f"{p}_requests_total",
                "Total routing requests",
                ["model", "status"],
            ),
            "latency": Histogram(
                f"{p}_request_latency_seconds",
                "Request latency in seconds",
                ["model"],
                buckets=(0.1, 0.25, 0.5, 1.0, 2.5, 5.0, 10.0, 30.0, 60.0),
            ),
            "tokens_total": Counter(
                f"{p}_tokens_total",
                "Total tokens consumed",
                ["model", "type"],
            ),
            "retries_total": Counter(
                f"{p}_retries_total",
                "Total retry attempts",
                ["model"],
            ),
            "errors_total": Counter(
                f"{p}_errors_total",
                "Total errors by type",
                ["model", "error_type"],
            ),
        }

    async def handle_event(self, event: RoutingEvent) -> None:
        """处理事件并更新指标"""
        et = event.event_type

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
        if event.latency is not None:
            self._latency.observe(event.latency)
            self._latency_by_model[event.model].observe(event.latency)
        if event.chunk_count:
            self._total_chunks += event.chunk_count
        # 吐字速率
        if event.chunk_count and event.latency and event.latency > 0:
            self._tokens_per_sec.observe(event.chunk_count / event.latency)
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

    def get_stats(self) -> Dict[str, Any]:
        """返回当前所有指标快照"""
        # 计算 QPS（最近 60s）
        now = time.time()
        cutoff = now - 60
        while self._request_timestamps and self._request_timestamps[0] < cutoff:
            self._request_timestamps.popleft()
        qps = len(self._request_timestamps) / 60.0

        return {
            "total_requests": self._request_count,
            "successful": self._success_count,
            "failed": self._failure_count,
            "retries": self._retry_count,
            "exhausted": self._exhausted_count,
            "streams": self._stream_count,
            "total_chunks": self._total_chunks,
            "qps": round(qps, 2),
            "tokens_per_sec": self._tokens_per_sec.to_dict(),
            "latency": self._latency.to_dict(),
            "ttft": self._ttft.to_dict(),
            "tokens": {
                "prompt": self._total_prompt_tokens,
                "completion": self._total_completion_tokens,
                "total": self._total_prompt_tokens + self._total_completion_tokens,
            },
            "by_model": {
                model: {
                    "requests": self._model_requests[model],
                    "successes": self._model_successes.get(model, 0),
                    "failures": self._model_failures.get(model, 0),
                    "latency": self._latency_by_model[model].to_dict()
                    if model in self._latency_by_model else None,
                    "ttft": self._ttft_by_model[model].to_dict()
                    if model in self._ttft_by_model else None,
                }
                for model in self._model_requests
            },
            "by_deployment": {
                dep_id: {
                    "provider": self._deployment_provider.get(dep_id, ""),
                    "requests": self._deployment_requests[dep_id],
                    "successes": self._deployment_successes.get(dep_id, 0),
                    "failures": self._deployment_failures.get(dep_id, 0),
                    "latency": self._deployment_latency[dep_id].to_dict()
                    if dep_id in self._deployment_latency else None,
                    "tokens": self._deployment_tokens.get(dep_id, 0),
                }
                for dep_id in self._deployment_requests
            },
            "errors_by_type": dict(self._errors_by_type),
            "timeline": list(self._timeline),
        }

    def expose_prometheus(self, port: int = 9090, addr: str = "0.0.0.0") -> None:
        """启动 Prometheus HTTP 服务，暴露指标供 Grafana 等 scrape

        Args:
            port: HTTP 服务端口，默认 9090
            addr: 绑定地址，默认 0.0.0.0

        用法:
            collector = MetricsCollector(enable_prometheus=True)
            collector.expose_prometheus(port=9090)
            # 然后 curl http://localhost:9090/metrics
        """
        if not self._enable_prometheus:
            self._init_prometheus()
            self._enable_prometheus = True

        try:
            from prometheus_client import start_http_server
        except ImportError:
            raise ImportError(
                "prometheus-client is required for Prometheus metrics. "
                "Install with: pip install intelli-router[metrics]"
            )

        start_http_server(port, addr=addr)

    def reset(self) -> None:
        """重置所有内存指标"""
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
        self._ttft = LatencyStats()
        self._ttft_by_model.clear()
        self._tokens_per_sec = LatencyStats()
        self._request_timestamps.clear()
        self._timeline.clear()
