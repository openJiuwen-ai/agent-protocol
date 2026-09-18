"""Tests for intelli_router.observability.web_dashboard."""
import json
import time
import urllib.request
import pytest
from intelli_router.observability.metrics import MetricsCollector
from intelli_router.observability.web_dashboard import MetricsWebServer
from intelli_router.observability.events import RoutingEvent, RoutingEventType
from intelli_router.observability.bus import EventBus


@pytest.fixture
def metrics():
    return MetricsCollector()


@pytest.fixture
def server(metrics):
    srv = MetricsWebServer(metrics, port=18932)
    srv.start()
    time.sleep(0.1)  # 等待服务就绪
    yield srv
    srv.stop()


class TestMetricsWebServer:
    def test_start_stop(self, metrics):
        srv = MetricsWebServer(metrics, port=18933)
        srv.start()
        time.sleep(0.05)
        srv.stop()

    def test_url_property(self, metrics):
        srv = MetricsWebServer(metrics, port=8888)
        assert srv.url == "http://localhost:8888"

    def test_get_root_html(self, server):
        resp = urllib.request.urlopen(f"{server.url}/")
        assert resp.status == 200
        content_type = resp.headers.get("Content-Type")
        assert "text/html" in content_type
        body = resp.read().decode()
        assert "IntelliRouter Metrics" in body
        assert "Chart" in body

    def test_get_api_stats_json(self, server):
        resp = urllib.request.urlopen(f"{server.url}/api/stats")
        assert resp.status == 200
        content_type = resp.headers.get("Content-Type")
        assert "application/json" in content_type
        data = json.loads(resp.read().decode())
        assert "total_requests" in data
        assert "latency" in data
        assert "tokens" in data

    def test_api_stats_reflects_data(self, server, metrics):
        """验证 /api/stats 返回的数据与 metrics 一致"""
        import asyncio

        async def emit_events():
            bus = EventBus()
            bus.register(metrics)
            await bus.emit(RoutingEvent(
                event_type=RoutingEventType.REQUEST_STARTED,
                request_id="test1", model="gpt-4",
            ))
            await bus.emit(RoutingEvent(
                event_type=RoutingEventType.REQUEST_SUCCEEDED,
                request_id="test1", model="gpt-4",
                deployment_id="dep-1", latency=0.5,
                prompt_tokens=100, completion_tokens=50,
            ))

        asyncio.run(emit_events())

        resp = urllib.request.urlopen(f"{server.url}/api/stats")
        data = json.loads(resp.read().decode())
        assert data["total_requests"] == 1
        assert data["successful"] == 1
        assert data["tokens"]["total"] == 150

    def test_get_metrics_endpoint(self, server):
        """GET /metrics 返回某种格式的指标"""
        resp = urllib.request.urlopen(f"{server.url}/metrics")
        assert resp.status == 200
        body = resp.read().decode()
        # 不管有没有 prometheus_client，都应该有输出
        assert len(body) > 0

    def test_404_unknown_path(self, server):
        """未知路径返回 404"""
        try:
            urllib.request.urlopen(f"{server.url}/unknown")
            assert False, "Should have raised"
        except urllib.error.HTTPError as e:
            assert e.code == 404

    def test_multiple_start_stop(self, metrics):
        """多次 start/stop 不报错"""
        srv = MetricsWebServer(metrics, port=18934)
        srv.start()
        time.sleep(0.05)
        srv.stop()
        srv.start()
        time.sleep(0.05)
        srv.stop()

    # -------- port validation (issue #47) --------

    @pytest.mark.parametrize("bad_port", [-1, 65536, 100000])
    def test_port_out_of_range_raises(self, metrics, bad_port):
        with pytest.raises(ValueError, match="port"):
            MetricsWebServer(metrics, port=bad_port)

    @pytest.mark.parametrize("bad_port", ["8080", 80.5, True])
    def test_port_non_int_raises(self, metrics, bad_port):
        with pytest.raises(TypeError, match="port"):
            MetricsWebServer(metrics, port=bad_port)

    def test_port_boundary_values_valid(self, metrics):
        """0（动态分配）与边界值 1/65535 应可创建。"""
        MetricsWebServer(metrics, port=0)
        MetricsWebServer(metrics, port=1)
        MetricsWebServer(metrics, port=65535)

    # -------- port=0 dynamic assignment (issue #46) --------

    def test_port_zero_dynamic_assignment(self, metrics):
        """port=0 时应由 OS 动态分配端口，且不再停留在 0。"""
        srv = MetricsWebServer(metrics, port=0)
        assert srv._port == 0  # start 前仍为 0
        srv.start()
        try:
            assert srv._port != 0
            assert 1 <= srv._port <= 65535
            assert srv.url.endswith(f":{srv._port}")
            # 动态分配的端口应可实际访问
            resp = urllib.request.urlopen(f"{srv.url}/api/stats")
            assert resp.status == 200
        finally:
            srv.stop()
