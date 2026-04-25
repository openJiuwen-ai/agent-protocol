# 前端设计文档

前端基于 **React 18 + TypeScript + Vite + Tailwind CSS + D3.js**，构建产物由后端静态托管，无独立服务器。

```
src/frontend/src/
├── App.tsx                          # 根组件：模式切换、全局状态
├── types/index.ts                   # TypeScript 接口定义
├── methodColors.ts                  # 各搜索方法配色
├── hooks/
│   └── useSearch.ts                 # 搜索 Hook（WebSocket / HTTP）
└── components/
    ├── DatasetSelector.tsx          # 数据集选择按钮组
    ├── QueryInput.tsx               # 查询输入框 + 示例建议
    ├── MethodSelector.tsx           # 搜索方法多选
    ├── SearchResultCard.tsx         # 单方法结果卡片（含树动画）
    ├── ResultPanel.tsx              # 结果列表展示
    ├── ComparisonChart.tsx          # 跨方法对比 + LLM 相关性判断
    ├── TreeAnimation.tsx            # D3.js 分类树实时动画
    ├── ServiceBrowser.tsx           # 数据集服务浏览器
    └── AdminPanel.tsx               # 管理员面板（注册/注销/构建）
```

---

## 整体架构

### 模式切换

应用在两个模式间切换，通过 `App.tsx` 中的 `mode` 状态控制：

```
mode = "search"  →  左侧边栏（数据集/查询/方法选择）+ 右侧结果面板
mode = "admin"   →  AdminPanel（注册/注销/构建管理）
```

### 启动预热

应用挂载后立即启动轮询，等待后端预热完成再渲染主界面：

```typescript
// App.tsx
useEffect(() => {
  const timer = setInterval(async () => {
    const data = await fetch("/api/warmup-status").then(r => r.json());
    if (data.ready) { setWarmupReady(true); clearInterval(timer); }
  }, 500);
}, []);
```

预热完成后依次加载：提供商列表、数据集列表、分类树。

---

## 搜索流程

### 方法路由（`hooks/useSearch.ts`）

搜索 Hook 根据方法名前缀选择传输协议：

| 方法前缀 | 协议 | 原因 |
|----------|------|------|
| `a2x_*`  | WebSocket | 分阶段推送导航步骤，驱动树动画 |
| `vector_*` / `traditional` | HTTP POST | 结果一次性返回，无需流式 |

```typescript
function searchOne(query, method, dataset, top_k) {
  if (method.startsWith("a2x")) {
    // WebSocket 流式
    const ws = new WebSocket(`ws://localhost:8000/api/search/ws`);
    ws.onopen = () => ws.send(JSON.stringify({ query, method, dataset, top_k }));
    ws.onmessage = (e) => {
      const msg = JSON.parse(e.data);
      if (msg.type === "step")   onStep(tag, msg.data);    // 更新树动画
      if (msg.type === "result") onResult(tag, msg.data);  // 显示结果
      if (msg.type === "error")  onError(tag, msg.message);
    };
  } else {
    // HTTP POST 同步
    const data = await fetch("/api/search", {
      method: "POST",
      body: JSON.stringify({ query, method, dataset, top_k })
    }).then(r => r.json());
    onResult(tag, data);
  }
}
```

多方法并行搜索：`App.tsx` 对每个选中方法分别调用 `searchOne()`，结果独立渲染在各自的 `SearchResultCard` 中。

### WebSocket 消息协议

```
客户端 →  { query, method, dataset, top_k }
服务端 ←  { type: "step",   data: { parent_id, selected[], pruned[] } }  (多次)
服务端 ←  { type: "result", data: { results[], stats{}, elapsed_time } } (一次)
         连接关闭
```

### 资源清理

```typescript
// useSearch.ts
const cancelAll = () => {
  activeWs.forEach(ws => ws.close());
  activeWs.clear();
};

// 组件卸载时
useEffect(() => () => cancelAll(), []);
```

---

## 管理员面板（AdminPanel）

### 状态机

```
op: "register" | "deregister" | "list" | "build"
isPolling: boolean  — 构建 SSE 流是否活跃
```

### API 调用汇总

| 操作 | 方法 | 端点 |
|------|------|------|
| 加载数据集列表 | GET | `/api/datasets` |
| 注册通用服务 | POST | `/api/datasets/{dataset}/services/generic` |
| 注册 A2A Agent | POST | `/api/datasets/{dataset}/services/a2a` |
| 注销服务 | DELETE | `/api/datasets/{dataset}/services/{service_id}` |
| 查询服务列表 | GET | `/api/datasets/{dataset}/services?fields=detail&size={n}&page={p}`（分页元数据通过响应 header `X-Total-Count` / `X-Page` / `X-Total-Pages`） |
| 管理员条目列表 | GET | `/api/datasets/{dataset}/services?fields=detail` |
| 单条精确查询 | GET | `/api/datasets/{dataset}/services/{service_id}`（skill 类型返回 ZIP） |
| 查询构建状态 | GET | `/api/datasets/{dataset}/build/status` |
| 启动构建 | POST | `/api/datasets/{dataset}/build` |
| 取消构建 | DELETE | `/api/datasets/{dataset}/build` |
| 构建日志流 | EventSource | `/api/datasets/{dataset}/build/stream` |

### 构建 SSE 流生命周期

```
启动构建 (POST /build)
  └→ openStream(dataset)
       ├─ 关闭已有 EventSource
       ├─ 清空 streamLogsRef
       └─ new EventSource("/api/datasets/{dataset}/build/stream")
            ├─ onmessage: type="log"    → 追加到 streamLogsRef，更新 response 显示
            ├─ onmessage: type="status" → 更新最终状态，关闭 EventSource，setIsPolling(false)
            └─ onerror                  → 关闭 EventSource，setIsPolling(false)
```

**断线重连（自动恢复）：** 切换到已有运行中构建的数据集时，自动重连 SSE 并回放历史日志：

```typescript
// 切换数据集时检查是否有运行中的构建
useEffect(() => {
  if (!dataset) return;
  fetch(`/api/datasets/${dataset}/build/status`)
    .then(r => r.json())
    .then(data => { if (data.status === "running") setOp("build"); });
}, [dataset]);

// 进入 build tab 且无活跃流时自动连接
useEffect(() => {
  if (op !== "build" || !dataset || esRef.current !== null) return;
  fetch(`/api/datasets/${dataset}/build/status`)
    .then(r => r.json())
    .then(data => { if (data.status === "running") openStream(dataset); });
}, [op, dataset, openStream]);
```

**切换数据集时清理：**

```typescript
useEffect(() => {
  if (esRef.current) { esRef.current.close(); esRef.current = null; }
  streamLogsRef.current = [];
  setResponse(null);
  setShowBuildLogs(false);
}, [dataset]);
```

### 按钮状态

```typescript
// 构建运行中：显示"中止构建"（灰色）
// 其他情况：显示操作对应的提交按钮
op === "build" && isPolling
  ? <button onClick={handleCancelBuild}>中止构建</button>
  : <button onClick={handleSubmit}>启动构建 / 注册服务 / ...</button>
```

`handleCancelBuild` 发送 `DELETE /api/datasets/{dataset}/build`，SSE 流收到 `status:cancelled` 后自动关闭。

---

## 其他组件的 API 调用

### DatasetSelector
```typescript
// 挂载时 + refreshKey 变化时
GET /api/datasets
```

### ServiceBrowser
```typescript
// 挂载时 + dataset / refreshKey 变化时
GET /api/datasets/{dataset}/services?mode=browse
```

返回 `{id, name, description}[]`，渲染为可折叠列表。

### ComparisonChart
```typescript
// 用户点击"判断相关性"按钮时
POST /api/search/judge
Body: { query, services: [ {id, name, description}, ... ] }
```

收集所有已完成方法的结果，合并去重后发送，LLM 逐一标注是否相关。

### App.tsx — 提供商切换
```typescript
// 点击提供商选项时
POST /api/providers/{name}
// 成功后更新 currentProvider 状态，所有后续 LLM 调用使用新提供商
```

### App.tsx — 分类树加载
```typescript
// dataset 变化时
GET /api/datasets/{dataset}/taxonomy
// 加载后存入 taxonomy state，传给 TreeAnimation 和 SearchResultCard
```

---

## 状态管理原则

- **无全局状态库**（不使用 Redux / Zustand），全部 `useState` + `useRef` + `useCallback`
- **`useRef` 用于跨渲染的可变值**：`esRef`（EventSource）、`streamLogsRef`（日志缓冲）、`startRef`（计时起点），避免闭包陈旧引用
- **`useCallback` + 稳定依赖**：`openStream`、`handleCancelBuild` 使用 `useCallback`，防止 effect 依赖数组中的函数引用频繁变化触发不必要的重连
- **数据集隔离**：所有 SSE 流、日志缓冲、响应状态均在 `[dataset]` effect 中清空，每个数据集状态独立

---

## 构建与部署

```bash
# 开发模式（Vite 热更新）
python -m a2x_registry.ui   # 自动启动 Vite dev server（port 5173）+ 后端（port 8000）

# 生产构建
python -m a2x_registry.frontend  # 执行 npm run build，产物输出到 src/frontend/dist/

# 生产模式（后端托管静态文件）
python -m a2x_registry.ui   # 检测到 dist/ 存在，后端直接托管，访问 http://localhost:8000
```
