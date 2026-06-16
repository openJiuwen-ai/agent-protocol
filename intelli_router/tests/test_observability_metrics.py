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

    async def test_tokens_per_sec_stream(self, collector):
        await collector.handle_event(_event(RoutingEventType.STREAM_STARTED))
        await collector.handle_event(_event(
            RoutingEventType.STREAM_SUCCEEDED,
            deployment_id="dep-1",
            latency=1.0,
            chunk_count=80,
        ))
        stats = collector.get_stats()
        # 80 chunks / 1s = 80 tokens/s
        assert stats["tokens_per_sec"]["count"] == 1
        assert stats["tokens_per_sec"]["avg"] == 80.0

    async def test_prometheus_import_error(self):
        """enable_prometheus=True without prometheus_client raises ImportError."""
        # 注意：如果环境中已安装 prometheus-client 此测试会被跳过
        if importlib.util.find_spec("prometheus_client") is not None:
            pytest.skip("prometheus-client is installed")
        with pytest.raises(ImportError, match="prometheus-client"):
            MetricsCollector(enable_prometheus=True)
