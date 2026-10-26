"""Tests for intelli_router.observability.metrics."""
import asyncio
import importlib.util
import threading
import time

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


async def _seed_two_deployments(collector):
    """喂出 d1/d2 两个部署的统计，使六个 _deployment_* 字典都含两键。

    d1 走 RETRIED + SUCCEEDED（requests/failures/successes 全有，
    SUCCEEDED 携带 provider/latency/tokens 补齐其余三个字典）；
    d2 走 SUCCEEDED。同步上下文可用 asyncio.run() 调用本函数。
    """
    await collector.handle_event(_event(RoutingEventType.REQUEST_STARTED))
    await collector.handle_event(_event(
        RoutingEventType.REQUEST_RETRIED,
        deployment_id="d1",
        error_type="TimeoutError",
    ))
    await collector.handle_event(_event(
        RoutingEventType.REQUEST_SUCCEEDED,
        deployment_id="d1",
        provider="openai",
        latency=0.4,
        prompt_tokens=10,
        completion_tokens=5,
    ))
    await collector.handle_event(_event(
        RoutingEventType.REQUEST_SUCCEEDED,
        deployment_id="d2",
        provider="anthropic",
        latency=0.6,
        prompt_tokens=20,
        completion_tokens=8,
    ))


def _all_deployment_dicts(collector):
    """收集 collector 的全部六个 _deployment_* 统计字典。"""
    return [
        collector._deployment_requests,
        collector._deployment_successes,
        collector._deployment_failures,
        collector._deployment_latency,
        collector._deployment_tokens,
        collector._deployment_provider,
    ]


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

    async def test_by_deployment_includes_failed_deployment(self, collector):
        """意见13: d1 失败一次后重试成功 d2，by_deployment 应同时含
        d1（requests>=1, failures=1）和 d2——修复前只遍历
        _deployment_requests（仅成功回调写入），d1 整条丢失。"""
        await collector.handle_event(_event(RoutingEventType.REQUEST_STARTED))
        await collector.handle_event(_event(
            RoutingEventType.REQUEST_RETRIED,
            deployment_id="d1",
            error_type="TimeoutError",
        ))
        await collector.handle_event(_event(
            RoutingEventType.REQUEST_SUCCEEDED,
            deployment_id="d2",
            latency=0.3,
        ))
        stats = collector.get_stats()
        by_dep = stats["by_deployment"]
        assert set(by_dep.keys()) == {"d1", "d2"}
        # d1: 被尝试过 1 次、失败 1 次、未成功
        assert by_dep["d1"]["requests"] == 1
        assert by_dep["d1"]["failures"] == 1
        assert by_dep["d1"]["successes"] == 0
        # d2: 被尝试过 1 次、成功 1 次
        assert by_dep["d2"]["requests"] == 1
        assert by_dep["d2"]["successes"] == 1
        assert by_dep["d2"]["failures"] == 0
        # 语义约束：RETRIED 不改变全局 failed 口径（仅 EXHAUSTED 累加）
        assert stats["failed"] == 0
        assert stats["retries"] == 1

    async def test_by_deployment_requests_equals_attempts(self, collector):
        """意见13: by_deployment.requests = 该部署被尝试次数
        = successes + failures。"""
        await collector.handle_event(_event(
            RoutingEventType.REQUEST_RETRIED, deployment_id="d1",
            error_type="TimeoutError"))
        await collector.handle_event(_event(
            RoutingEventType.REQUEST_RETRIED, deployment_id="d1",
            error_type="TimeoutError"))
        await collector.handle_event(_event(
            RoutingEventType.REQUEST_SUCCEEDED, deployment_id="d1",
            latency=0.2))
        by_dep = collector.get_stats()["by_deployment"]["d1"]
        assert by_dep["requests"] == 3
        assert by_dep["failures"] == 2
        assert by_dep["successes"] == 1

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

    # -------- STREAM_CANCELLED 终态计数（意见C，配合流式取消事件） --------

    async def test_stream_cancelled_counter(self, collector):
        """STARTED + CANCELLED：cancelled==1，successful/failed 均为 0，
        total 在 STARTED 时已计（不动）。"""
        await collector.handle_event(_event(RoutingEventType.STREAM_STARTED))
        await collector.handle_event(_event(
            RoutingEventType.STREAM_CANCELLED,
            extra={"reason": "consumer_break"},
        ))
        stats = collector.get_stats()
        assert stats["cancelled"] == 1
        assert stats["successful"] == 0
        assert stats["failed"] == 0
        assert stats["total_requests"] == 1
        assert stats["streams"] == 1
        # 配对不变式扩展：successful + failed + cancelled + 在途 == total
        # （此处无在途，三者之和应恰等于 total）
        assert (
            stats["successful"] + stats["failed"] + stats["cancelled"]
            == stats["total_requests"]
        )

    async def test_stream_cancelled_does_not_touch_latency_or_deployment(
            self, collector):
        """取消≠成功：不 observe 全局延迟、不计 by_deployment
        （requests = successes + failures 口径不含取消）。"""
        await collector.handle_event(_event(RoutingEventType.STREAM_STARTED))
        await collector.handle_event(_event(
            RoutingEventType.STREAM_CANCELLED,
            deployment_id="d1",
            latency=0.5,
            chunk_count=7,
        ))
        stats = collector.get_stats()
        assert stats["cancelled"] == 1
        assert stats["latency"]["count"] == 0
        assert stats["by_deployment"] == {}

    async def test_stream_cancelled_timeline_entry(self, collector):
        """取消流在时间线留一条 stream_cancelled 记录（可携带
        reason/latency/chunks）。"""
        await collector.handle_event(_event(RoutingEventType.STREAM_STARTED))
        await collector.handle_event(_event(
            RoutingEventType.STREAM_CANCELLED,
            deployment_id="d1",
            latency=0.5,
            chunk_count=7,
            extra={"reason": "consumer_break"},
        ))
        entries = [
            e for e in collector.get_stats()["timeline"]
            if e["type"] == "stream_cancelled"
        ]
        assert len(entries) == 1
        assert entries[0]["deployment"] == "d1"
        assert entries[0]["latency"] == 0.5
        assert entries[0]["chunks"] == 7
        assert entries[0]["reason"] == "consumer_break"

    async def test_stream_cancelled_reset(self, collector):
        await collector.handle_event(_event(RoutingEventType.STREAM_STARTED))
        await collector.handle_event(_event(
            RoutingEventType.STREAM_CANCELLED))
        assert collector.get_stats()["cancelled"] == 1
        collector.reset()
        assert collector.get_stats()["cancelled"] == 0

    # -------- DEPLOYMENTS_UPDATED 热替换键清理（意见A） --------

    async def test_deployments_updated_removes_stale_keys(self, collector):
        """热替换事件 removed_ids=["d1"]：by_deployment 只剩 d2，
        六个 _deployment_* 字典均无 d1 残留，且所有计数器不变。"""
        await _seed_two_deployments(collector)
        before = collector.get_stats()
        assert set(before["by_deployment"]) == {"d1", "d2"}

        await collector.handle_event(_event(
            RoutingEventType.DEPLOYMENTS_UPDATED,
            extra={"removed_ids": ["d1"]},
        ))

        after = collector.get_stats()
        assert set(after["by_deployment"]) == {"d2"}
        # d2 各字段保持不变
        assert after["by_deployment"]["d2"] == before["by_deployment"]["d2"]
        # 全部 _deployment_* 字典无 d1 键（LatencyStats 随字典一并删除）
        for dep_dict in _all_deployment_dicts(collector):
            assert "d1" not in dep_dict
        # 纯键清理：任何计数器与其他维度统计都不动
        for key in (
            "total_requests", "successful", "failed", "cancelled",
            "retries", "exhausted", "streams", "total_chunks",
        ):
            assert after[key] == before[key]
        assert after["by_model"] == before["by_model"]
        assert after["tokens"] == before["tokens"]
        assert after["latency"] == before["latency"]
        assert after["errors_by_type"] == before["errors_by_type"]

    async def test_deployments_updated_without_valid_removed_ids_is_ignored(
            self, collector):
        """removed_ids 缺失 / 非 list / 含未知 id：防御性忽略，无异常，
        统计完全不变。"""
        await _seed_two_deployments(collector)
        before = collector.get_stats()

        # extra 缺失 removed_ids
        await collector.handle_event(_event(
            RoutingEventType.DEPLOYMENTS_UPDATED))
        # removed_ids 非 list
        await collector.handle_event(_event(
            RoutingEventType.DEPLOYMENTS_UPDATED,
            extra={"removed_ids": "d1"},
        ))
        # removed_ids 里的未知 id：静默无操作
        await collector.handle_event(_event(
            RoutingEventType.DEPLOYMENTS_UPDATED,
            extra={"removed_ids": ["nope"]},
        ))

        assert collector.get_stats() == before

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
        assert "cancelled" in stats
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

    async def test_stream_tokens_counted_when_usage_reported(self, collector):
        """流式事件携带真实 usage 时，token 应被统计（意见 8 后半句）。

        router 从流式 usage 中提取 token 数放入 STREAM_SUCCEEDED 事件；
        此前流式输出完全不计入 tokens 统计。chunk 冒充 token 的场景
        由上一个测试守护（不带 token 的事件不统计）。
        """
        await collector.handle_event(_event(RoutingEventType.STREAM_STARTED))
        await collector.handle_event(_event(
            RoutingEventType.STREAM_SUCCEEDED,
            deployment_id="dep-1",
            latency=2.0,
            chunk_count=80,
            prompt_tokens=1000,
            completion_tokens=40,
        ))
        stats = collector.get_stats()
        assert stats["tokens"]["prompt"] == 1000
        assert stats["tokens"]["completion"] == 40
        # by_deployment 的 tokens 同步累计
        assert stats["by_deployment"]["dep-1"]["tokens"] == 1040
        # 真实 token 的吐字速率：40 tokens / 2s = 20 tokens/s
        assert stats["tokens_per_sec"]["count"] == 1
        assert stats["tokens_per_sec"]["avg"] == 20.0
        # chunk 速率独立统计
        assert stats["chunks_per_sec"]["count"] == 1
        assert stats["chunks_per_sec"]["avg"] == 40.0

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


class TestDeploymentKeyPruning:
    """意见A 兜底 API：remove_deployment / prune_deployments 供不走
    event_bus 或同步上下文的宿主调用。"""

    def test_remove_deployment(self):
        collector = MetricsCollector()
        asyncio.run(_seed_two_deployments(collector))
        collector.remove_deployment("d1")
        stats = collector.get_stats()
        assert set(stats["by_deployment"]) == {"d2"}
        for dep_dict in _all_deployment_dicts(collector):
            assert "d1" not in dep_dict

    def test_remove_deployment_nonexistent_is_noop(self):
        """删除不存在的 id 静默忽略，不抛异常、不改统计。"""
        collector = MetricsCollector()
        asyncio.run(_seed_two_deployments(collector))
        before = collector.get_stats()
        collector.remove_deployment("nope")
        assert collector.get_stats() == before

    def test_prune_deployments(self):
        """keep_ids 之外的部署键全部清除。"""
        collector = MetricsCollector()
        asyncio.run(_seed_two_deployments(collector))
        collector.prune_deployments({"d2"})
        stats = collector.get_stats()
        assert set(stats["by_deployment"]) == {"d2"}
        for dep_dict in _all_deployment_dicts(collector):
            assert "d1" not in dep_dict

    def test_prune_deployments_accepts_list_and_empty_keep(self):
        """keep_ids 接受任意可迭代（list/set）；空保留集合 = 全部清除。"""
        collector = MetricsCollector()
        asyncio.run(_seed_two_deployments(collector))
        # list 形式的保留集合同样有效
        collector.prune_deployments(["d1", "d2"])
        assert set(collector.get_stats()["by_deployment"]) == {"d1", "d2"}
        # 空集合：所有部署视为下线，全部清除
        collector.prune_deployments(set())
        assert collector.get_stats()["by_deployment"] == {}
        for dep_dict in _all_deployment_dicts(collector):
            assert not dep_dict


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


class TestThreadSafety:
    """意见14: MetricsCollector 是 EventBus handler（请求协程并发调用），
    同时 ThreadingHTTPServer /api/stats 线程并发读 get_stats——统计
    字典须有锁保护，否则 keys() | keys() 等遍历会抛
    "dictionary changed size during iteration"。"""

    def _feed_events(self, collector, n_events, tag, errors):
        """在独立事件循环里喂 n_events 组（STARTED + RETRIED/SUCCEEDED），
        任何异常收集到 errors。"""
        async def run():
            for i in range(n_events):
                model = f"model-{tag}-{i % 7}"
                dep = f"dep-{tag}-{i % 5}"
                await collector.handle_event(RoutingEvent(
                    event_type=RoutingEventType.REQUEST_STARTED,
                    request_id=f"{tag}-{i}", model=model,
                    timestamp=time.time(),
                ))
                if i % 2 == 0:
                    await collector.handle_event(RoutingEvent(
                        event_type=RoutingEventType.REQUEST_RETRIED,
                        request_id=f"{tag}-{i}", model=model,
                        deployment_id=dep, error_type="TimeoutError",
                        timestamp=time.time(),
                    ))
                else:
                    await collector.handle_event(RoutingEvent(
                        event_type=RoutingEventType.REQUEST_SUCCEEDED,
                        request_id=f"{tag}-{i}", model=model,
                        deployment_id=dep, latency=0.1,
                        timestamp=time.time(),
                    ))
        try:
            asyncio.run(run())
        except Exception as e:  # noqa: BLE001 - 收集后统一断言
            errors.append(e)

    def test_concurrent_handle_event_and_get_stats(self):
        """多线程并发喂事件 + 并发 get_stats 不抛异常，且记账不丢不重。"""
        collector = MetricsCollector()
        errors = []
        stop = threading.Event()

        def reader():
            # 模拟 ThreadingHTTPServer 的 /api/stats 线程持续读快照
            while not stop.is_set():
                try:
                    stats = collector.get_stats()
                    assert "by_deployment" in stats
                    for dep_stats in stats["by_deployment"].values():
                        # requests = successes + failures 的不变式
                        # 在任意快照时刻都应成立
                        assert (
                            dep_stats["requests"]
                            >= dep_stats["successes"]
                        )
                except Exception as e:  # noqa: BLE001
                    errors.append(e)
                    return

        readers = [threading.Thread(target=reader) for _ in range(3)]
        for t in readers:
            t.start()
        writers = [
            threading.Thread(
                target=self._feed_events, args=(collector, 150, f"w{i}", errors))
            for i in range(4)
        ]
        for t in writers:
            t.start()
        for t in writers:
            t.join(timeout=60)
        stop.set()
        for t in readers:
            t.join(timeout=10)

        assert not errors, f"concurrent access raised: {errors!r}"
        # 写入全部完成：total = STARTED 数；终态（RETRIED/SUCCEEDED）与
        # STARTED 一一配对
        stats = collector.get_stats()
        assert stats["total_requests"] == 4 * 150
        assert stats["retries"] + stats["successful"] == 4 * 150
        # by_deployment 的 requests 同样守恒（每对事件恰好一次尝试）
        total_dep_requests = sum(
            d["requests"] for d in stats["by_deployment"].values())
        assert total_dep_requests == 4 * 150

    def test_concurrent_reset_and_get_stats(self):
        """reset 与 get_stats/记账并发不抛异常。"""
        collector = MetricsCollector()
        self._feed_events(collector, 50, "seed", [])
        errors = []
        stop = threading.Event()

        def churn():
            while not stop.is_set():
                try:
                    collector.reset()
                    collector.get_stats()
                except Exception as e:  # noqa: BLE001
                    errors.append(e)
                    return

        threads = [threading.Thread(target=churn) for _ in range(3)]
        for t in threads:
            t.start()
        self._feed_events(collector, 100, "w", errors)
        stop.set()
        for t in threads:
            t.join(timeout=10)
        assert not errors, errors


class TestPrometheusRegistry:
    """意见12: MetricsCollector 支持自定义 prometheus registry，
    同进程多个 collector 可并存（指标名只由 prefix 决定，全部注册
    全局默认 registry 时同 prefix 第二个实例抛 DuplicateTimeseries）。"""

    def _prom_available(self):
        if importlib.util.find_spec("prometheus_client") is None:
            pytest.skip("prometheus-client is not installed")

    def _succeed(self, collector):
        asyncio.run(collector.handle_event(RoutingEvent(
            event_type=RoutingEventType.REQUEST_SUCCEEDED,
            request_id="r1", model="gpt-4",
            deployment_id="d1", latency=0.1, timestamp=1000.0,
        )))

    def test_two_collectors_custom_registries_no_conflict(self):
        """同 prefix 的两个 collector 用不同自定义 registry 不冲突，
        且计数相互隔离。"""
        self._prom_available()
        from prometheus_client import CollectorRegistry, generate_latest

        c1 = MetricsCollector(
            enable_prometheus=True, prometheus_prefix="multi_coll",
            prometheus_registry=CollectorRegistry())
        c2 = MetricsCollector(
            enable_prometheus=True, prometheus_prefix="multi_coll",
            prometheus_registry=CollectorRegistry())
        assert c1._registry is not c2._registry

        # 事件只计入 c1 的 registry（c2 的同名指标存在但值为 0）
        self._succeed(c1)
        out1 = generate_latest(c1._registry).decode()
        assert (
            'multi_coll_requests_total{model="gpt-4",status="success"} 1.0'
            in out1
        )
        assert c1._registry.get_sample_value(
            "multi_coll_requests_total",
            {"model": "gpt-4", "status": "success"}) == 1.0
        assert c2._registry.get_sample_value(
            "multi_coll_requests_total",
            {"model": "gpt-4", "status": "success"}) is None

    def test_custom_registry_not_registered_globally(self):
        """自定义 registry 的指标不进全局默认 registry（隔离）。"""
        self._prom_available()
        from prometheus_client import REGISTRY, CollectorRegistry

        c = MetricsCollector(
            enable_prometheus=True, prometheus_prefix="iso_check",
            prometheus_registry=CollectorRegistry())
        self._succeed(c)
        assert c._registry.get_sample_value(
            "iso_check_requests_total",
            {"model": "gpt-4", "status": "success"}) == 1.0
        assert REGISTRY.get_sample_value(
            "iso_check_requests_total",
            {"model": "gpt-4", "status": "success"}) is None

    def test_default_registry_backward_compatible(self):
        """不传 prometheus_registry 时保持旧行为：注册到全局默认 registry。"""
        self._prom_available()
        from prometheus_client import REGISTRY

        c = MetricsCollector(
            enable_prometheus=True, prometheus_prefix="compat_def_reg")
        assert c._registry is None
        self._succeed(c)
        assert REGISTRY.get_sample_value(
            "compat_def_reg_requests_total",
            {"model": "gpt-4", "status": "success"}) == 1.0

    def test_expose_prometheus_serves_custom_registry(self):
        """expose_prometheus 也挂在 collector 自己的 registry 上。"""
        self._prom_available()
        import urllib.request
        from prometheus_client import CollectorRegistry

        c = MetricsCollector(
            enable_prometheus=True, prometheus_prefix="expose_custom",
            prometheus_registry=CollectorRegistry())
        self._succeed(c)
        server, thread = c.expose_prometheus(port=0)
        try:
            body = urllib.request.urlopen(
                f"http://127.0.0.1:{server.server_port}/metrics").read()
            assert b"expose_custom_requests_total" in body
        finally:
            server.shutdown()
            server.server_close()
            thread.join(timeout=5)


# -------- 复核修复：EXHAUSTED 的最终失败尝试归因 --------


class TestExhaustedFinalAttemptAttribution:
    """最终失败尝试（其后无 RETRIED）此前在 by_deployment 整条丢失；
    EXHAUSTED 事件 extra.route_id（last_failure_metadata）归因补齐，
    维持 requests = successes + failures 等式。"""

    @pytest.mark.asyncio
    async def test_exhausted_attributes_final_attempt_to_route_id(self, collector):
        # d1 失败一次有 RETRIED；d2 最终失败只有 EXHAUSTED（extra.route_id）
        await collector.handle_event(_event(RoutingEventType.REQUEST_STARTED))
        await collector.handle_event(_event(
            RoutingEventType.REQUEST_RETRIED, deployment_id="d1", error_type="ValueError",
        ))
        await collector.handle_event(_event(
            RoutingEventType.ALL_DEPLOYMENTS_EXHAUSTED,
            extra={"route_id": "d2"},
        ))
        stats = collector.get_stats()
        by_dep = stats["by_deployment"]
        assert by_dep["d1"]["requests"] == 1
        assert by_dep["d1"]["failures"] == 1
        # 最终失败尝试经 route_id 归因到 d2，不再丢失
        assert by_dep["d2"]["requests"] == 1
        assert by_dep["d2"]["failures"] == 1
        assert stats["failed"] == 1  # 全局 failed 仅 EXHAUSTED 累加

    @pytest.mark.asyncio
    async def test_exhausted_without_route_id_no_attribution(self, collector):
        """extra 无 route_id（防御旧版本事件流）：不归因、不抛异常。"""
        await collector.handle_event(_event(RoutingEventType.REQUEST_STARTED))
        await collector.handle_event(_event(RoutingEventType.ALL_DEPLOYMENTS_EXHAUSTED))
        stats = collector.get_stats()
        assert stats["by_deployment"] == {}
        assert stats["failed"] == 1

    @pytest.mark.asyncio
    async def test_request_cancelled_counts_cancelled(self, collector):
        """非流式取消计入 cancelled（与 STREAM_CANCELLED 共用计数器），
        不加 successful/failed、不动 total。"""
        await collector.handle_event(_event(RoutingEventType.REQUEST_STARTED))
        await collector.handle_event(_event(RoutingEventType.REQUEST_CANCELLED))
        stats = collector.get_stats()
        assert stats["cancelled"] == 1
        assert stats["successful"] == 0
        assert stats["failed"] == 0
        assert stats["total_requests"] == 1
        # 四终态口径：successful + failed + cancelled == total（在途 0）
        assert stats["successful"] + stats["failed"] + stats["cancelled"] == stats["total_requests"]
        # 时间线可追溯
        assert any(e["type"] == "request_cancelled" for e in stats["timeline"])
