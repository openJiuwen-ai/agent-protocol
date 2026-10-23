"""Tests for intelli_router.observability.metrics."""
import importlib.util

import pytest
from intelli_router.observability.metrics import MetricsCollector, LatencyStats
from intelli_router.observability.events import RoutingEvent, RoutingEventType


@pytest.fixture
def collector():
    return MetricsCollector()


def _event(event_type, **kwargs):
    defaults = {"request_id": "test123", "model": "gpt-4", "timestamp": 1000.0}
    defaults.update(kwargs)
    return RoutingEvent(event_type=event_type, **defaults)


class TestLatencyStats:
    def test_empty(self):
        s = LatencyStats()
        assert s.count == 0
        assert s.avg == 0.0
        d = s.to_dict()
        assert d["count"] == 0
        assert d["min"] is None
        assert d["p50"] is None
        assert d["p95"] is None
        assert d["p99"] is None

    def test_observe_single(self):
        s = LatencyStats()
        s.observe(1.5)
        assert s.count == 1
        assert s.avg == 1.5
        assert s.min_val == 1.5
        assert s.max_val == 1.5

    def test_observe_multiple(self):
        s = LatencyStats()
        s.observe(1.0)
        s.observe(2.0)
        s.observe(3.0)
        assert s.count == 3
        assert s.avg == 2.0
        assert s.min_val == 1.0
        assert s.max_val == 3.0

    def test_percentiles(self):
        s = LatencyStats()
        # 观测 1-100 的值
        for i in range(1, 101):
            s.observe(float(i))
        # idx = int(100 * p/100), 所以 p50 → idx 50 → value 51
        assert s.percentile(50) == 51.0
        assert s.percentile(95) == 96.0
        assert s.percentile(99) == 100.0
        d = s.to_dict()
        assert d["p50"] == 51.0
        assert d["p95"] == 96.0
        assert d["p99"] == 100.0

    def test_sample_limit(self):
        s = LatencyStats()
        # 超过 1000 个样本时保留最近的
        for i in range(1200):
            s.observe(float(i))
        assert len(s._samples) == 1000
        # 最早的样本应该是 200
        assert s._samples[0] == 200.0


@pytest.mark.asyncio
class TestMetricsCollector:
    async def test_request_started(self, collector):
        await collector.handle_event(_event(RoutingEventType.REQUEST_STARTED))
        stats = collector.get_stats()
        assert stats["total_requests"] == 1

    async def test_request_succeeded(self, collector):
        await collector.handle_event(_event(RoutingEventType.REQUEST_STARTED))
        await collector.handle_event(_event(
            RoutingEventType.REQUEST_SUCCEEDED,
            deployment_id="dep-1",
            latency=0.5,
            prompt_tokens=100,
            completion_tokens=50,
        ))
        stats = collector.get_stats()
        assert stats["successful"] == 1
        assert stats["latency"]["count"] == 1
        assert stats["latency"]["avg"] == 0.5
        assert stats["tokens"]["prompt"] == 100
        assert stats["tokens"]["completion"] == 50
        assert stats["tokens"]["total"] == 150
        assert stats["by_deployment"]["dep-1"]["requests"] == 1
        assert stats["by_deployment"]["dep-1"]["successes"] == 1
        assert stats["by_deployment"]["dep-1"]["latency"]["avg"] == 0.5

    async def test_request_retried(self, collector):
        await collector.handle_event(_event(
            RoutingEventType.REQUEST_RETRIED,
            error_type="TimeoutError",
            error_message="timed out",
        ))
        stats = collector.get_stats()
        assert stats["retries"] == 1
        assert stats["errors_by_type"]["TimeoutError"] == 1

    async def test_all_deployments_exhausted(self, collector):
        await collector.handle_event(_event(RoutingEventType.ALL_DEPLOYMENTS_EXHAUSTED))
        stats = collector.get_stats()
        assert stats["exhausted"] == 1

    async def test_exhausted_counts_as_failed(self, collector):
        """意见13: exhausted 即最终失败，failed 必须递增（此前恒为 0）。"""
        await collector.handle_event(_event(RoutingEventType.REQUEST_STARTED))
        await collector.handle_event(_event(RoutingEventType.ALL_DEPLOYMENTS_EXHAUSTED))
        stats = collector.get_stats()
        assert stats["failed"] == 1
        assert stats["exhausted"] == 1

    async def test_exhausted_increments_by_model_failures(self, collector):
        """意见13: by_model 的 failures 随最终失败递增。"""
        await collector.handle_event(_event(
            RoutingEventType.REQUEST_STARTED, model="gpt-4"))
        await collector.handle_event(_event(
            RoutingEventType.ALL_DEPLOYMENTS_EXHAUSTED, model="gpt-4"))
        stats = collector.get_stats()
        assert stats["by_model"]["gpt-4"]["failures"] == 1

    async def test_retry_is_not_final_failure(self, collector):
        """意见13 口径: 重试≠最终失败，failed 不应在 retried 时递增
        （避免与 successful 重复计数）。"""
        await collector.handle_event(_event(
            RoutingEventType.REQUEST_RETRIED, error_type="TimeoutError"))
        stats = collector.get_stats()
        assert stats["retries"] == 1
        assert stats["failed"] == 0

    async def test_failed_accumulates_across_exhaustions(self, collector):
        """多次最终失败累计计数，reset 归零。"""
        for _ in range(3):
            await collector.handle_event(_event(
                RoutingEventType.ALL_DEPLOYMENTS_EXHAUSTED, model="gpt-4"))
        stats = collector.get_stats()
        assert stats["failed"] == 3
        assert stats["by_model"]["gpt-4"]["failures"] == 3
        collector.reset()
        stats = collector.get_stats()
        assert stats["failed"] == 0
        assert stats["by_model"] == {}

    async def test_stream_lifecycle(self, collector):
        await collector.handle_event(_event(RoutingEventType.STREAM_STARTED))
        await collector.handle_event(_event(
            RoutingEventType.STREAM_SUCCEEDED,
            deployment_id="dep-1",
            latency=2.0,
            chunk_count=42,
        ))
        stats = collector.get_stats()
        assert stats["streams"] == 1
        assert stats["total_requests"] == 1
        assert stats["successful"] == 1
        assert stats["total_chunks"] == 42
        assert stats["latency"]["count"] == 1

    async def test_multiple_models(self, collector):
        await collector.handle_event(_event(
            RoutingEventType.REQUEST_STARTED, model="gpt-4"))
        await collector.handle_event(_event(
            RoutingEventType.REQUEST_STARTED, model="claude-3"))
        await collector.handle_event(_event(
            RoutingEventType.REQUEST_SUCCEEDED, model="gpt-4", latency=0.5))
        await collector.handle_event(_event(
            RoutingEventType.REQUEST_SUCCEEDED, model="claude-3", latency=1.0))

        stats = collector.get_stats()
        assert stats["by_model"]["gpt-4"]["requests"] == 1
        assert stats["by_model"]["claude-3"]["requests"] == 1
        assert stats["by_model"]["gpt-4"]["successes"] == 1

    async def test_errors_by_type(self, collector):
        await collector.handle_event(_event(
            RoutingEventType.REQUEST_RETRIED, error_type="TimeoutError"))
        await collector.handle_event(_event(
            RoutingEventType.REQUEST_RETRIED, error_type="TimeoutError"))
        await collector.handle_event(_event(
            RoutingEventType.REQUEST_RETRIED, error_type="ConnectionError"))

        stats = collector.get_stats()
        assert stats["errors_by_type"]["TimeoutError"] == 2
        assert stats["errors_by_type"]["ConnectionError"] == 1

    async def test_reset(self, collector):
        await collector.handle_event(_event(RoutingEventType.REQUEST_STARTED))
        await collector.handle_event(_event(
            RoutingEventType.REQUEST_SUCCEEDED, latency=1.0, prompt_tokens=10, completion_tokens=5))

        collector.reset()
        stats = collector.get_stats()

        assert stats["total_requests"] == 0
        assert stats["successful"] == 0
        assert stats["tokens"]["total"] == 0
        assert stats["latency"]["count"] == 0

    async def test_get_stats_structure(self, collector):
        stats = collector.get_stats()
        assert "total_requests" in stats
        assert "successful" in stats
        assert "failed" in stats
        assert "retries" in stats
        assert "exhausted" in stats
        assert "streams" in stats
        assert "total_chunks" in stats
        assert "latency" in stats
        assert "tokens" in stats
        assert "by_model" in stats
        assert "by_deployment" in stats
        assert "errors_by_type" in stats
        assert "qps" in stats
        assert "tokens_per_sec" in stats
        assert "chunks_per_sec" in stats

    async def test_qps(self, collector):
        import time
        now = time.time()
        # 模拟最近 60s 内的 10 个请求
        for i in range(10):
            await collector.handle_event(_event(
                RoutingEventType.REQUEST_STARTED, timestamp=now - i))
        stats = collector.get_stats()
        assert stats["qps"] == pytest.approx(10 / 60.0, abs=0.01)

    async def test_qps_excludes_old(self, collector):
        import time
        now = time.time()
        # 旧请求 (>60s 前) 不应计入
        await collector.handle_event(_event(
            RoutingEventType.REQUEST_STARTED, timestamp=now - 120))
        await collector.handle_event(_event(
            RoutingEventType.REQUEST_STARTED, timestamp=now - 5))
        stats = collector.get_stats()
        assert stats["qps"] == pytest.approx(1 / 60.0, abs=0.01)

    async def test_tokens_per_sec(self, collector):
        await collector.handle_event(_event(RoutingEventType.REQUEST_STARTED))
        await collector.handle_event(_event(
            RoutingEventType.REQUEST_SUCCEEDED,
            latency=2.0,
            completion_tokens=100,
        ))
        stats = collector.get_stats()
        # 100 tokens / 2s = 50 tokens/s
        assert stats["tokens_per_sec"]["count"] == 1
        assert stats["tokens_per_sec"]["avg"] == 50.0

    async def test_tokens_per_sec_stream_not_polluted_by_chunks(self, collector):
        """流式 chunk 速率应进入 chunks_per_sec，不污染 tokens_per_sec。

        chunk ≠ token，若混入 tokens_per_sec 会冒充吐字速率（意见14）。
        """
        await collector.handle_event(_event(RoutingEventType.STREAM_STARTED))
        await collector.handle_event(_event(
            RoutingEventType.STREAM_SUCCEEDED,
            deployment_id="dep-1",
            latency=1.0,
            chunk_count=80,
        ))
        stats = collector.get_stats()
        # 80 chunks / 1s = 80 chunks/s
        assert stats["chunks_per_sec"]["count"] == 1
        assert stats["chunks_per_sec"]["avg"] == 80.0
        # tokens_per_sec 不应被流式 chunk 样本污染
        assert stats["tokens_per_sec"]["count"] == 0
        assert stats["tokens_per_sec"]["avg"] == 0.0

    async def test_chunks_per_sec_and_tokens_per_sec_independent(self, collector):
        """流式与非流式样本分别进入两个独立序列。"""
        # 非流式：100 tokens / 2s = 50 tokens/s
        await collector.handle_event(_event(RoutingEventType.REQUEST_STARTED))
        await collector.handle_event(_event(
            RoutingEventType.REQUEST_SUCCEEDED,
            latency=2.0,
            completion_tokens=100,
        ))
        # 流式：80 chunks / 1s = 80 chunks/s
        await collector.handle_event(_event(RoutingEventType.STREAM_STARTED))
        await collector.handle_event(_event(
            RoutingEventType.STREAM_SUCCEEDED,
            latency=1.0,
            chunk_count=80,
        ))
        stats = collector.get_stats()
        assert stats["tokens_per_sec"]["count"] == 1
        assert stats["tokens_per_sec"]["avg"] == 50.0
        assert stats["chunks_per_sec"]["count"] == 1
        assert stats["chunks_per_sec"]["avg"] == 80.0

    async def test_prometheus_import_error(self):
        """enable_prometheus=True without prometheus_client raises ImportError."""
        # 注意：如果环境中已安装 prometheus-client 此测试会被跳过
        if importlib.util.find_spec("prometheus_client") is not None:
            pytest.skip("prometheus-client is installed")
        with pytest.raises(ImportError, match="prometheus-client"):
            MetricsCollector(enable_prometheus=True)


class TestPrometheusPrefixValidation:
    """issue #48: prometheus_prefix 校验。"""

    def test_empty_prefix_raises(self):
        with pytest.raises(ValueError, match="prometheus_prefix"):
            MetricsCollector(prometheus_prefix="")

    def test_valid_prefix_accepted(self):
        MetricsCollector(prometheus_prefix="my_router")
        MetricsCollector(prometheus_prefix="a")
        MetricsCollector(prometheus_prefix="A_b2")

    @pytest.mark.parametrize("bad_prefix", [
        "1abc",          # 数字开头
        "-abc",          # 非法字符开头
        "my-router",     # 连字符不合法
        "my router",     # 空格不合法
        "abc.def",       # 点号不合法
    ])
    def test_invalid_prefix_raises(self, bad_prefix):
        with pytest.raises(ValueError, match="prometheus_prefix"):
            MetricsCollector(prometheus_prefix=bad_prefix)

    def test_too_long_prefix_raises(self):
        with pytest.raises(ValueError, match="prometheus_prefix"):
            MetricsCollector(prometheus_prefix="a" * 257)

    def test_prefix_length_256_accepted(self):
        MetricsCollector(prometheus_prefix="a" * 256)

    def test_non_string_prefix_raises(self):
        with pytest.raises(TypeError, match="prometheus_prefix"):
            MetricsCollector(prometheus_prefix=123)


class TestExposePrometheus:
    """意见17: expose_prometheus 默认绑定 127.0.0.1，并返回可停止的服务句柄。"""

    def _prom_available(self):
        if importlib.util.find_spec("prometheus_client") is None:
            pytest.skip("prometheus-client is not installed")
        return True

    def test_default_addr_is_loopback(self):
        """默认 addr 应为 127.0.0.1（仅本机），而非 0.0.0.0。"""
        import inspect
        from intelli_router.observability.metrics import MetricsCollector as MC
        sig = inspect.signature(MC.expose_prometheus)
        assert sig.parameters["addr"].default == "127.0.0.1"

    def test_expose_prometheus_returns_handle_and_stops(self):
        """返回 (server, thread) 句柄，且句柄可用于停止服务。"""
        self._prom_available()
        import urllib.request
        collector = MetricsCollector(
            enable_prometheus=True, prometheus_prefix="test_expose")
        result = collector.expose_prometheus(port=0)
        assert isinstance(result, tuple) and len(result) == 2
        server, thread = result
        try:
            # 句柄已存到实例属性
            assert collector._prom_server is server
            assert collector._prom_thread is thread
            port = server.server_port
            assert port != 0
            body = urllib.request.urlopen(
                f"http://127.0.0.1:{port}/metrics").read()
            assert len(body) > 0
        finally:
            server.shutdown()
            server.server_close()
            thread.join(timeout=5)
        # 停止后端口不再可访问
        try:
            urllib.request.urlopen(f"http://127.0.0.1:{port}/metrics", timeout=2)
            reachable = True
        except Exception:
            reachable = False
        assert not reachable

    def test_expose_prometheus_default_returns_handle(self):
        """默认参数（无 enable_prometheus）也能启动并返回句柄。"""
        self._prom_available()
        collector = MetricsCollector(prometheus_prefix="test_expose2")
        server, thread = collector.expose_prometheus(port=0)
        try:
            assert server.server_port != 0
        finally:
            server.shutdown()
            server.server_close()
            thread.join(timeout=5)
