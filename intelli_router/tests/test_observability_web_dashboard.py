"""Tests for intelli_router.observability.web_dashboard."""
import asyncio
import json
import os
import re
import shutil
import subprocess
import tempfile
import time
import urllib.request
import pytest
from intelli_router.observability.metrics import MetricsCollector
from intelli_router.observability.web_dashboard import MetricsWebServer, _DASHBOARD_HTML
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

    # -------- binding & CORS (issue #17) --------

    def test_default_addr_is_loopback(self, metrics):
        """默认 addr 应为 127.0.0.1（仅本机），而非 0.0.0.0。"""
        import inspect
        sig = inspect.signature(MetricsWebServer.__init__)
        assert sig.parameters["addr"].default == "127.0.0.1"
        srv = MetricsWebServer(metrics, port=18940)
        assert srv._addr == "127.0.0.1"

    def test_default_addr_bindable_and_served(self, metrics):
        """默认 127.0.0.1 上应可正常启动并提供服务。"""
        srv = MetricsWebServer(metrics, port=0)
        srv.start()
        try:
            resp = urllib.request.urlopen(f"{srv.url}/api/stats")
            assert resp.status == 200
        finally:
            srv.stop()

    def test_cors_disabled_by_default(self, server):
        """cors_origins=None（默认）不发送 Access-Control-Allow-Origin 头。"""
        resp = urllib.request.urlopen(f"{server.url}/api/stats")
        assert resp.status == 200
        assert resp.headers.get("Access-Control-Allow-Origin") is None

    # -------- CORS origin matching (review fix on issue #17) --------
    # 旧实现把 list 多源用 ",".join 拼成单个 ACAO 值，浏览器一律拒绝；
    # 新语义：白名单命中请求 Origin 后只回显该单个源。

    def test_cors_list_whitelist_origin_hit(self, metrics):
        """list 白名单 + Origin 命中：ACAO 等于命中的那个源（非逗号拼接），
        且响应带 Vary: Origin。"""
        srv = MetricsWebServer(
            metrics, port=0,
            cors_origins=["https://a.com", "https://b.com"])
        srv.start()
        try:
            req = urllib.request.Request(
                f"{srv.url}/api/stats", headers={"Origin": "https://b.com"})
            resp = urllib.request.urlopen(req)
            assert resp.status == 200
            assert (
                resp.headers.get("Access-Control-Allow-Origin") == "https://b.com"
            )
            assert resp.headers.get("Vary") == "Origin"
        finally:
            srv.stop()

    def test_cors_list_whitelist_origin_miss(self, metrics):
        """list 白名单 + Origin 未命中：不发送 ACAO 头。"""
        srv = MetricsWebServer(
            metrics, port=0,
            cors_origins=["https://a.com", "https://b.com"])
        srv.start()
        try:
            req = urllib.request.Request(
                f"{srv.url}/api/stats", headers={"Origin": "https://evil.com"})
            resp = urllib.request.urlopen(req)
            assert resp.status == 200
            assert resp.headers.get("Access-Control-Allow-Origin") is None
        finally:
            srv.stop()

    def test_cors_list_whitelist_no_origin_header(self, metrics):
        """list 白名单 + 请求不带 Origin 头：不发送 ACAO 头。"""
        srv = MetricsWebServer(
            metrics, port=0,
            cors_origins=["https://a.com", "https://b.com"])
        srv.start()
        try:
            resp = urllib.request.urlopen(f"{srv.url}/api/stats")
            assert resp.status == 200
            assert resp.headers.get("Access-Control-Allow-Origin") is None
        finally:
            srv.stop()

    def test_cors_single_str_origin_hit(self, metrics):
        """单个 str 源 + Origin 匹配：ACAO 等于该源，且带 Vary: Origin。"""
        srv = MetricsWebServer(
            metrics, port=0, cors_origins="https://example.com")
        srv.start()
        try:
            req = urllib.request.Request(
                f"{srv.url}/api/stats",
                headers={"Origin": "https://example.com"})
            resp = urllib.request.urlopen(req)
            assert resp.status == 200
            assert (
                resp.headers.get("Access-Control-Allow-Origin")
                == "https://example.com"
            )
            assert resp.headers.get("Vary") == "Origin"
        finally:
            srv.stop()

    def test_cors_single_str_origin_mismatch(self, metrics):
        """单个 str 源 + Origin 不匹配：不发送 ACAO 头。"""
        srv = MetricsWebServer(
            metrics, port=0, cors_origins="https://example.com")
        srv.start()
        try:
            req = urllib.request.Request(
                f"{srv.url}/api/stats",
                headers={"Origin": "https://other.com"})
            resp = urllib.request.urlopen(req)
            assert resp.status == 200
            assert resp.headers.get("Access-Control-Allow-Origin") is None
        finally:
            srv.stop()

    def test_cors_wildcard_with_origin(self, metrics):
        """"*" 通配 + 带 Origin 请求：无条件发 ACAO: *。"""
        srv = MetricsWebServer(metrics, port=0, cors_origins="*")
        srv.start()
        try:
            req = urllib.request.Request(
                f"{srv.url}/api/stats", headers={"Origin": "https://any.com"})
            resp = urllib.request.urlopen(req)
            assert resp.status == 200
            assert resp.headers.get("Access-Control-Allow-Origin") == "*"
        finally:
            srv.stop()

    def test_cors_origins_invalid_type_raises(self, metrics):
        with pytest.raises(TypeError, match="cors_origins"):
            MetricsWebServer(metrics, port=18941, cors_origins=123)

    def test_url_property_explicit_addr(self, metrics):
        """显式传入非通配地址时 url 显示真实地址。"""
        srv = MetricsWebServer(metrics, port=8888, addr="192.168.1.10")
        assert srv.url == "http://192.168.1.10:8888"
        srv0 = MetricsWebServer(metrics, port=8888, addr="0.0.0.0")
        assert srv0.url == "http://localhost:8888"
        srv_loop = MetricsWebServer(metrics, port=8888, addr="127.0.0.1")
        assert srv_loop.url == "http://localhost:8888"

    # -------- threading HTTP server (issue #16) --------

    def test_server_is_threading_http_server(self, metrics):
        """服务实例应为 ThreadingHTTPServer 且 daemon_threads 开启。"""
        from http.server import ThreadingHTTPServer
        srv = MetricsWebServer(metrics, port=0)
        srv.start()
        try:
            assert isinstance(srv._httpd, ThreadingHTTPServer)
            assert srv._httpd.daemon_threads is True
        finally:
            srv.stop()

    # -------- stop() closes the listening socket (issue #18) --------

    def test_stop_releases_port_for_rebind(self, metrics):
        """stop() 后监听 socket 应立即关闭，端口可立即重绑。"""
        srv = MetricsWebServer(metrics, port=0)
        srv.start()
        port = srv._port
        srv.stop()
        assert srv._httpd is None
        # 直接在同端口重新 bind 验证 socket 已释放
        import socket
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 0)
            s.bind(("127.0.0.1", port))
        # 且能再次以同端口启动
        srv2 = MetricsWebServer(metrics, port=port)
        srv2.start()
        try:
            resp = urllib.request.urlopen(f"{srv2.url}/api/stats")
            assert resp.status == 200
        finally:
            srv2.stop()

    # -------- DOM XSS in table rendering (issue #15) --------

    def _inline_js(self):
        scripts = re.findall(r"<script>(.*?)</script>", _DASHBOARD_HTML, re.DOTALL)
        assert scripts, "dashboard HTML should contain inline script"
        return scripts[0]

    def test_tables_render_without_innerhtml_concat(self):
        """三张表的渲染不得使用 innerHTML += 模板拼接（XSS 向量）。"""
        js = self._inline_js()
        assert "innerHTML +=" not in js
        assert "innerHTML+=" not in js
        # 表格渲染使用 DOM API + textContent 逐单元格填充
        assert "insertCell" in js
        assert "insertRow" in js
        assert "textContent = cell" in js

    def test_tables_use_dom_api_not_innerhtml(self):
        """update() 渲染三张表时完全不依赖 innerHTML。"""
        js = self._inline_js()
        assert "innerHTML" not in js, (
            "table rendering should use DOM APIs (insertRow/insertCell/"
            "textContent), not innerHTML"
        )

    def test_malicious_model_name_rendered_as_text(self, metrics):
        """行为测试：恶意 model 名经 stats 渲染后以文本形式存在，
        不会成为可执行标记。无 node 环境时跳过。"""
        node = shutil.which("node")
        if node is None:
            pytest.skip("node is not available for JS behavior test")

        async def emit():
            bus = EventBus()
            bus.register(metrics)
            evil = '<img src=x onerror=alert(1)>'
            await bus.emit(RoutingEvent(
                event_type=RoutingEventType.REQUEST_STARTED,
                request_id="x1", model=evil))
            await bus.emit(RoutingEvent(
                event_type=RoutingEventType.REQUEST_SUCCEEDED,
                request_id="x1", model=evil,
                deployment_id="dep-x", latency=0.1))
            await bus.emit(RoutingEvent(
                event_type=RoutingEventType.REQUEST_RETRIED,
                request_id="x1", model=evil,
                error_type="<script>alert(2)</script>"))

        asyncio.run(emit())
        stats = metrics.get_stats()
        evil = '<img src=x onerror=alert(1)>'

        rendered = self._run_node_harness(node, stats)
        # 恶意字符串以纯文本出现在渲染产物中（textContent 不解析标记）
        assert evil in rendered
        assert "<script>alert(2)</script>" in rendered
        # 渲染产物中不存在可执行的 onerror 属性语义：
        # textContent 赋值产物是纯文本节点，这里 dump 出的
        # 是 textContent 的拼接，不应产生新的可执行节点。
        # 若用 innerHTML 拼接，evil 会被解析为元素而非文本。

    # -------- refresh clears table rows (review regression) --------

    def test_tables_clear_rows_on_repeated_update(self, metrics):
        """行为测试：同一数据连续两次 update() 后三张表行数不翻倍。
        回归保护：XSS 修复曾删除刷新前清空 tbody 的逻辑，导致
        2 秒轮询下行无限累积。无 node 环境时跳过。"""
        node = shutil.which("node")
        if node is None:
            pytest.skip("node is not available for JS behavior test")

        async def emit():
            bus = EventBus()
            bus.register(metrics)
            await bus.emit(RoutingEvent(
                event_type=RoutingEventType.REQUEST_STARTED,
                request_id="r1", model="gpt-4"))
            await bus.emit(RoutingEvent(
                event_type=RoutingEventType.REQUEST_SUCCEEDED,
                request_id="r1", model="gpt-4",
                deployment_id="dep-1", latency=0.2))
            await bus.emit(RoutingEvent(
                event_type=RoutingEventType.REQUEST_RETRIED,
                request_id="r1", model="gpt-4",
                error_type="TimeoutError"))

        asyncio.run(emit())
        stats = metrics.get_stats()

        # 连续两次全量刷新同一份数据，行数不应随刷新次数增长
        # （每张表恰好 1 行数据：dep-1 / gpt-4 / TimeoutError）
        rendered = self._run_node_harness(node, stats, updates=2)
        assert "deploy:1 model:1 error:1" in rendered

    def test_js_clears_tbody_before_fill(self):
        """静态断言：表格渲染前有清空 tbody 的逻辑。"""
        js = self._inline_js()
        # 存在清空函数，且三张表填充前都调用
        assert "function clearRows" in js
        assert "deleteRow(-1)" in js
        assert js.count("clearRows(") >= 4  # 1 处定义 + 3 处调用
        # 不得退回 innerHTML 清空/拼接（XSS 向量）
        assert "innerHTML" not in js

    # -------- node harness helpers --------

    def _run_node_harness(self, node, stats, updates=1):
        """在 node 里执行看板 JS：模拟 DOM、调用 updates 次 update(stats)，
        返回 stdout（表格 dump 与行数统计）。"""
        # 提取内联 JS（去掉自启动尾部）
        js = self._inline_js()
        js_funcs = re.sub(
            r"\ninitCharts\(\);\nfetchStats\(\);\nsetInterval\(fetchStats, 2000\);\s*$",
            "\n", js)
        assert "function update" in js_funcs
        assert "initCharts();\nfetchStats();" not in js_funcs

        # JSON 载荷安全转义后嵌入 harness
        stats_json = (
            json.dumps(stats, ensure_ascii=False)
            .replace("\\", "\\\\")
            .replace("'", "\\'")
        )
        harness = f"""
function makeEl(tag) {{
  return {{
    tagName: tag, children: [], textContent: '', className: '',
    cells: [], colSpan: 0,
    data: {{labels: [], datasets: [{{data: []}}]}},
    insertRow() {{ const r = makeEl('tr'); this.children.push(r); return r; }},
    insertCell() {{ const c = makeEl('td'); this.cells.push(c); this.children.push(c); return c; }},
    get rows() {{ return this.children; }},
    deleteRow() {{ this.children.pop(); }},
    getContext() {{ return {{}}; }},
    update() {{}},
  }};
}}
const els = {{}};
function el(id) {{ if (!els[id]) els[id] = makeEl('div'); return els[id]; }}
globalThis.document = {{ getElementById: el, createElement: (t) => makeEl(t) }};
class Chart {{
  static defaults = {{}};
  constructor(ctx, cfg) {{ this.data = {{labels: [], datasets: [{{data: []}}]}}; }}
  update() {{}}
}}
globalThis.Chart = Chart;

eval({json.dumps(js_funcs)});
initCharts();
const data = JSON.parse('{stats_json}');
for (let i = 0; i < {updates}; i++) update(data);
function dump(el) {{ return (el.textContent || '') + el.children.map(c => dump(c)).join(''); }}
console.log(JSON.stringify(dump(els['model-table'])));
console.log(JSON.stringify(dump(els['deploy-table'])));
console.log(JSON.stringify(dump(els['error-table'])));
console.log('deploy:' + els['deploy-table'].children.length
  + ' model:' + els['model-table'].children.length
  + ' error:' + els['error-table'].children.length);
"""
        with tempfile.NamedTemporaryFile(
            "w", suffix=".js", delete=False
        ) as f:
            f.write(harness)
            path = f.name
        try:
            r = subprocess.run(
                [node, path], capture_output=True, text=True, timeout=30)
        finally:
            os.unlink(path)
        assert r.returncode == 0, f"node harness failed:\n{r.stderr}"
        return r.stdout

    def test_dashboard_html_cards_unchanged(self, server):
        """卡片区仍走 textContent（回归保护）。"""
        resp = urllib.request.urlopen(f"{server.url}/")
        body = resp.read().decode()
        assert "textContent" in body
