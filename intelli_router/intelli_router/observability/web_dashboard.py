"""轻量 Web 指标看板 - 基于 stdlib http.server"""
import json
import threading
from http.server import HTTPServer, BaseHTTPRequestHandler
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from .metrics import MetricsCollector

_DASHBOARD_HTML = """\
<!DOCTYPE html>
<html lang="zh">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>IntelliRouter Metrics</title>
<script src="https://cdn.jsdelivr.net/npm/chart.js@4"></script>
<style>
* { margin: 0; padding: 0; box-sizing: border-box; }
body {
  font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Inter, sans-serif;
  background: #f8f9fa; color: #495057; padding: 32px 40px;
  line-height: 1.5; -webkit-font-smoothing: antialiased;
}
.header {
  display: flex; align-items: center; justify-content: space-between;
  margin-bottom: 32px;
}
.header h1 {
  font-size: 20px; font-weight: 600; color: #1a1a1a;
  letter-spacing: -0.3px;
}
.status {
  display: flex; align-items: center; gap: 8px;
  font-size: 12px; color: #868e96;
}
.status .dot {
  width: 7px; height: 7px; border-radius: 50%;
  background: #22c55e; box-shadow: 0 0 6px rgba(34,197,94,0.3);
  animation: pulse 2s infinite;
}
@keyframes pulse {
  0%, 100% { opacity: 1; }
  50% { opacity: 0.5; }
}
.cards {
  display: grid; grid-template-columns: repeat(auto-fit, minmax(170px, 1fr));
  gap: 16px; margin-bottom: 32px;
}
.card {
  background: #ffffff; border: 1px solid #e9ecef; border-radius: 12px;
  padding: 20px;
}
.card .value {
  font-size: 26px; font-weight: 700; color: #1a1a1a;
  letter-spacing: -0.5px;
}
.card .label {
  font-size: 11px; color: #868e96; margin-top: 6px;
  text-transform: uppercase; letter-spacing: 0.5px;
}
.card .sub {
  font-size: 11px; color: #868e96; margin-top: 4px;
}
.charts {
  display: grid; grid-template-columns: 2fr 1fr; gap: 16px; margin-bottom: 32px;
}
.chart-box {
  background: #ffffff; border: 1px solid #e9ecef;
  border-radius: 12px; padding: 20px;
}
.section {
  background: #ffffff; border: 1px solid #e9ecef;
  border-radius: 12px; padding: 20px; margin-bottom: 24px;
}
.section h2 {
  font-size: 13px; font-weight: 500; color: #1a1a1a;
  margin-bottom: 16px; letter-spacing: -0.2px;
}
table { width: 100%; border-collapse: collapse; }
th, td {
  padding: 10px 12px; text-align: left; font-size: 13px;
  border-bottom: 1px solid #e9ecef;
}
th { color: #495057; font-weight: 500; font-size: 11px;
     text-transform: uppercase; letter-spacing: 0.3px; }
td { color: #495057; }
tbody tr { transition: background 0.15s; }
tbody tr:hover { background: #f1f3f5; }
tbody tr:last-child td { border-bottom: none; }
.empty { color: #adb5bd; font-style: italic; }
@media (max-width: 768px) {
  body { padding: 20px 16px; }
  .charts { grid-template-columns: 1fr; }
}
</style>
</head>
<body>
<div class="header">
  <h1>IntelliRouter Metrics</h1>
  <div class="status"><span class="dot"></span><span id="status-text">Connecting...</span></div>
</div>
<div class="cards">
  <div class="card"><div class="value" id="total-req">-</div><div class="label">Requests</div></div>
  <div class="card"><div class="value" id="qps">-</div><div class="label">QPS (60s)</div></div>
  <div class="card"><div class="value" id="success-rate">-</div><div class="label">Success Rate</div></div>
  <div class="card"><div class="value" id="avg-latency">-</div><div class="label">Avg Latency</div><div class="sub" id="latency-pct"></div></div>
  <div class="card"><div class="value" id="tokens-per-sec">-</div><div class="label">Tokens/s</div></div>
  <div class="card"><div class="value" id="total-tokens">-</div><div class="label">Tokens</div></div>
  <div class="card"><div class="value" id="ttft">-</div><div class="label">Avg TTFT</div></div>
</div>
<div class="charts">
  <div class="chart-box"><canvas id="deployChart"></canvas></div>
  <div class="chart-box"><canvas id="outcomeChart"></canvas></div>
</div>
<div class="section">
  <h2>Latency Timeline</h2>
  <div style="height:200px"><canvas id="timelineChart"></canvas></div>
</div>
<div class="section">
  <h2>Deployments</h2>
  <table><thead><tr><th>Deployment</th><th>Provider</th><th>Requests</th><th>Success</th><th>Failures</th><th>Avg Latency</th><th>Tokens</th></tr></thead>
  <tbody id="deploy-table"></tbody></table>
</div>
<div class="section">
  <h2>Models</h2>
  <table><thead><tr><th>Model</th><th>Requests</th><th>Success</th><th>Failures</th><th>Avg Latency</th><th>TTFT</th></tr></thead>
  <tbody id="model-table"></tbody></table>
</div>
<div class="section">
  <h2>Errors</h2>
  <table><thead><tr><th>Type</th><th>Count</th></tr></thead>
  <tbody id="error-table"></tbody></table>
</div>

<script>
let deployChart, outcomeChart, timelineChart;
const ACCENT = '#6366f1';
const ACCENT_BG = 'rgba(99,102,241,0.08)';
const GRID_COLOR = '#e9ecef';
const TICK_COLOR = '#868e96';

function initCharts() {
  Chart.defaults.color = TICK_COLOR;
  Chart.defaults.borderColor = GRID_COLOR;

  const dCtx = document.getElementById('deployChart').getContext('2d');
  deployChart = new Chart(dCtx, {
    type: 'bar',
    data: { labels: [], datasets: [{ label: 'Requests', data: [], backgroundColor: ACCENT, borderRadius: 4 }] },
    options: { responsive: true, plugins: { title: { display: true, text: 'Requests by Deployment', color: '#1a1a1a', font: { size: 13, weight: '500' } },
      legend: { display: false } }, scales: { x: { grid: { display: false } }, y: { beginAtZero: true, grid: { color: GRID_COLOR } } } }
  });
  const oCtx = document.getElementById('outcomeChart').getContext('2d');
  outcomeChart = new Chart(oCtx, {
    type: 'doughnut',
    data: { labels: ['Success', 'Failed', 'Retries'], datasets: [{ data: [0,0,0],
      backgroundColor: ['#22c55e', '#ef4444', '#f59e0b'], borderWidth: 0 }] },
    options: { responsive: true, cutout: '70%', plugins: { title: { display: true, text: 'Outcomes', color: '#1a1a1a', font: { size: 13, weight: '500' } } } }
  });
  const tCtx = document.getElementById('timelineChart').getContext('2d');
  timelineChart = new Chart(tCtx, {
    type: 'line',
    data: { labels: [], datasets: [{ label: 'Latency (s)', data: [], borderColor: ACCENT,
      backgroundColor: ACCENT_BG, fill: true, tension: 0.4, pointRadius: 0, borderWidth: 2 }] },
    options: { responsive: true, maintainAspectRatio: false,
      interaction: { intersect: false, mode: 'index' },
      plugins: { legend: { display: false } },
      scales: { x: { grid: { display: false }, ticks: { maxTicksLimit: 10 } },
        y: { beginAtZero: true, grid: { color: GRID_COLOR },
        title: { display: true, text: 'seconds', color: '#52525b', font: { size: 11 } } } } }
  });
}

function formatTokens(n) {
  if (n >= 1000000) return (n/1000000).toFixed(1) + 'M';
  if (n >= 1000) return (n/1000).toFixed(1) + 'K';
  return n.toString();
}

function update(data) {
  const total = data.total_requests || 0;
  const successRate = total > 0 ? ((data.successful / total) * 100).toFixed(1) : '0.0';
  document.getElementById('total-req').textContent = total;
  document.getElementById('qps').textContent = data.qps !== undefined ? data.qps.toFixed(1) : '-';
  document.getElementById('success-rate').textContent = successRate + '%';
  document.getElementById('avg-latency').textContent = data.latency.count > 0 ? data.latency.avg.toFixed(3) + 's' : '-';
  const lp = data.latency;
  document.getElementById('latency-pct').textContent = lp.p50 != null ? `P50 ${lp.p50.toFixed(3)}s · P95 ${lp.p95.toFixed(3)}s · P99 ${lp.p99.toFixed(3)}s` : '';
  const tps = data.tokens_per_sec;
  document.getElementById('tokens-per-sec').textContent = tps && tps.count > 0 ? tps.avg.toFixed(0) : '-';
  document.getElementById('total-tokens').textContent = formatTokens(data.tokens.total);
  document.getElementById('ttft').textContent = data.ttft && data.ttft.count > 0 ? data.ttft.avg.toFixed(3) + 's' : '-';
  document.getElementById('status-text').textContent = 'Live · ' + new Date().toLocaleTimeString();

  const depLabels = Object.keys(data.by_deployment);
  const depValues = depLabels.map(k => data.by_deployment[k].requests || 0);
  deployChart.data.labels = depLabels;
  deployChart.data.datasets[0].data = depValues;
  deployChart.update();

  outcomeChart.data.datasets[0].data = [data.successful || 0, data.failed || 0, data.retries || 0];
  outcomeChart.update();

  const timeline = data.timeline || [];
  const recentWithLatency = timeline.filter(e => e.latency != null).slice(-50);
  timelineChart.data.labels = recentWithLatency.map(e => {
    const d = new Date(e.ts * 1000); return d.toLocaleTimeString();
  });
  timelineChart.data.datasets[0].data = recentWithLatency.map(e => e.latency);
  timelineChart.update();

  const dt = document.getElementById('deploy-table');
  dt.innerHTML = '';
  for (const [dep, stats] of Object.entries(data.by_deployment || {})) {
    const lat = stats.latency && stats.latency.count > 0 ? stats.latency.avg.toFixed(3) + 's' : '-';
    dt.innerHTML += `<tr><td>${dep}</td><td>${stats.provider || '-'}</td><td>${stats.requests}</td><td>${stats.successes}</td><td>${stats.failures}</td><td>${lat}</td><td>${formatTokens(stats.tokens || 0)}</td></tr>`;
  }
  if (!depLabels.length) dt.innerHTML = '<tr><td colspan="7" class="empty">No data yet</td></tr>';

  const mt = document.getElementById('model-table');
  mt.innerHTML = '';
  for (const [model, stats] of Object.entries(data.by_model || {})) {
    const lat = stats.latency && stats.latency.count > 0 ? stats.latency.avg.toFixed(3) + 's' : '-';
    const ttft = stats.ttft && stats.ttft.count > 0 ? stats.ttft.avg.toFixed(3) + 's' : '-';
    mt.innerHTML += `<tr><td>${model}</td><td>${stats.requests}</td><td>${stats.successes}</td><td>${stats.failures}</td><td>${lat}</td><td>${ttft}</td></tr>`;
  }
  if (!Object.keys(data.by_model || {}).length) mt.innerHTML = '<tr><td colspan="6" class="empty">No data yet</td></tr>';

  const et = document.getElementById('error-table');
  et.innerHTML = '';
  for (const [err, count] of Object.entries(data.errors_by_type || {})) {
    et.innerHTML += `<tr><td>${err}</td><td>${count}</td></tr>`;
  }
  if (!Object.keys(data.errors_by_type || {}).length) et.innerHTML = '<tr><td colspan="2" class="empty">No errors</td></tr>';
}

async function fetchStats() {
  try {
    const resp = await fetch('/api/stats');
    const data = await resp.json();
    update(data);
  } catch (e) { console.error('Fetch failed:', e); }
}

initCharts();
fetchStats();
setInterval(fetchStats, 2000);
</script>
</body>
</html>
"""


_DASHBOARD_HTML_BYTES = _DASHBOARD_HTML.encode("utf-8")


class MetricsWebServer:
    """轻量 Web 指标看板

    基于 Python stdlib http.server，提供一个实时刷新的 HTML 看板页面。
    零额外依赖，图表使用浏览器端 Chart.js CDN。

    用法:
        from intelli_router.observability import MetricsCollector, MetricsWebServer

        metrics = MetricsCollector()
        bus.register(metrics)

        server = MetricsWebServer(metrics, port=8080)
        server.start()
        # 浏览器打开 http://localhost:8080
        server.stop()
    """

    def __init__(
        self,
        metrics: "MetricsCollector",
        port: int = 8080,
        addr: str = "0.0.0.0",
    ):
        self._metrics = metrics
        self._port = port
        self._addr = addr
        self._thread = None
        self._httpd = None

    @property
    def url(self) -> str:
        """看板访问地址"""
        host = "localhost" if self._addr == "0.0.0.0" else self._addr
        return f"http://{host}:{self._port}"

    def start(self) -> None:
        """启动 HTTP 服务（后台 daemon 线程）"""
        metrics = self._metrics

        class Handler(BaseHTTPRequestHandler):
            def do_GET(self):
                if self.path == "/" or self.path == "/index.html":
                    self._serve_html()
                elif self.path == "/api/stats":
                    self._serve_json()
                elif self.path == "/metrics":
                    self._serve_metrics()
                else:
                    self.send_error(404)

            def _serve_html(self):
                self.send_response(200)
                self.send_header("Content-Type", "text/html; charset=utf-8")
                self.end_headers()
                self.wfile.write(_DASHBOARD_HTML_BYTES)

            def _serve_json(self):
                data = json.dumps(metrics.get_stats(), ensure_ascii=False)
                self.send_response(200)
                self.send_header("Content-Type", "application/json; charset=utf-8")
                self.send_header("Access-Control-Allow-Origin", "*")
                self.end_headers()
                self.wfile.write(data.encode("utf-8"))

            def _serve_metrics(self):
                try:
                    from prometheus_client import generate_latest, CONTENT_TYPE_LATEST
                    output = generate_latest()
                    self.send_response(200)
                    self.send_header("Content-Type", CONTENT_TYPE_LATEST)
                    self.end_headers()
                    self.wfile.write(output)
                except ImportError:
                    self.send_response(200)
                    self.send_header("Content-Type", "text/plain; charset=utf-8")
                    self.end_headers()
                    # 简单文本格式输出
                    stats = metrics.get_stats()
                    lines = [
                        f"# IntelliRouter Metrics (plain text, install prometheus-client for Prometheus format)",
                        f"total_requests {stats['total_requests']}",
                        f"successful {stats['successful']}",
                        f"failed {stats['failed']}",
                        f"retries {stats['retries']}",
                        f"exhausted {stats['exhausted']}",
                        f"avg_latency {stats['latency']['avg']}",
                        f"tokens_total {stats['tokens']['total']}",
                    ]
                    self.wfile.write("\n".join(lines).encode("utf-8"))

            def log_message(self, format, *args):
                pass  # 静默，不输出到 stderr

        self._httpd = HTTPServer((self._addr, self._port), Handler)
        self._thread = threading.Thread(target=self._httpd.serve_forever, daemon=True)
        self._thread.start()

    def stop(self) -> None:
        """停止 HTTP 服务"""
        if self._httpd:
            self._httpd.shutdown()
            self._httpd = None
        if self._thread:
            self._thread.join(timeout=5)
            self._thread = None
