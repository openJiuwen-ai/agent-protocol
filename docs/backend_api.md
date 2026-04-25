# 后端 API 文档

后端基于 FastAPI，启动命令：

```bash
python -m a2x_registry.backend
# 服务地址: http://127.0.0.1:8000
# 交互文档: http://127.0.0.1:8000/docs
```

## 路由总览

API 由 4 个路由模块 + 1 个应用级端点组成：

| 模块 | 前缀 | 源文件 | 说明 |
|------|------|--------|------|
| 数据集 | `/api/datasets` | `src/backend/routers/dataset.py` | 数据集 CRUD、服务注册/注销、分类树、嵌入配置 |
| 构建 | `/api/datasets/{dataset}/build` | `src/backend/routers/build.py` | 分类树构建触发、状态、取消、SSE 日志流 |
| 搜索 | `/api/search` | `src/backend/routers/search.py` | 同步搜索、WebSocket 流式搜索、LLM 相关性判断 |
| 提供商 | `/api/providers` | `src/backend/routers/provider.py` | LLM 提供商列表与切换 |
| 应用级 | `/api/warmup-status` | `src/backend/app.py` | 启动预热进度 |

---

## 1. 数据集路由 `/api/datasets`

### 端点一览

| 方法 | 路径 | 说明 |
|------|------|------|
| `GET` | `/api/datasets` | 列出所有数据集 |
| `POST` | `/api/datasets` | 创建新数据集 |
| `DELETE` | `/api/datasets/{dataset}` | 删除数据集 |
| `GET` | `/api/datasets/{dataset}/services` | 列出/筛选服务（`fields=brief\|detail`，分页 header） |
| `GET` | `/api/datasets/{dataset}/services/{service_id}` | 单条精确查询（skill 类型返回 ZIP） |
| `POST` | `/api/datasets/{dataset}/services/generic` | 注册通用服务 |
| `POST` | `/api/datasets/{dataset}/services/a2a` | 注册 A2A Agent |
| `PUT` | `/api/datasets/{dataset}/services/{service_id}` | 部分字段更新服务 |
| `DELETE` | `/api/datasets/{dataset}/services/{service_id}` | 注销服务 |
| `DELETE` | `/api/datasets/{dataset}/services/{service_id}/lease` | teammate-self 释放该 sid 上的 lease |
| `POST` | `/api/datasets/{dataset}/reservations` | 预订 N 个匹配 filters 的 service（lease） |
| `DELETE` | `/api/datasets/{dataset}/reservations/{holder_id}` | 释放 holder 的全部 lease（bulk） |
| `DELETE` | `/api/datasets/{dataset}/reservations/{holder_id}/{service_id}` | 释放单个 lease（per-sid） |
| `POST` | `/api/datasets/{dataset}/reservations/{holder_id}/extend` | 续期 holder 的全部 lease |
| `POST` | `/api/datasets/{dataset}/skills` | 上传 Skill（ZIP） |
| `DELETE` | `/api/datasets/{dataset}/skills/{name}` | 删除 Skill |
| `GET` | `/api/datasets/{dataset}/skills/{name}/download` | 下载 Skill（ZIP） |
| `GET` | `/api/datasets/{dataset}/taxonomy` | 获取分类树 |
| `GET` | `/api/datasets/{dataset}/default-queries` | 示例查询 |
| `GET` | `/api/datasets/embedding-models` | 支持的嵌入模型列表 |
| `GET` | `/api/datasets/{dataset}/vector-config` | 获取嵌入配置 |
| `POST` | `/api/datasets/{dataset}/vector-config` | 设置嵌入模型 |
| `GET` | `/api/datasets/{dataset}/register-config` | 获取允许的注册格式 |
| `POST` | `/api/datasets/{dataset}/register-config` | 覆盖允许的注册格式 |

---

### GET `/api/datasets`

列出所有数据集及其服务数量和查询数量。

**响应：**
```json
[
  { "name": "ToolRet_clean", "service_count": 1839, "query_count": 1714 },
  { "name": "publicMCP",   "service_count": 1387, "query_count": 50 }
]
```

---

### POST `/api/datasets`

创建一个新的空数据集目录并配置嵌入模型，同时可声明该数据集接受的注册格式。

**请求体：**
```json
{
  "name": "my_dataset",
  "embedding_model": "all-MiniLM-L6-v2",
  "formats": {
    "generic": "v0.0",
    "a2a":     "v0.0",
    "skill":   "v0.0"
  }
}
```

| 字段 | 必填 | 说明 |
|------|------|------|
| `name` | 是 | 数据集名 |
| `embedding_model` | 否 | 嵌入模型（默认 `"all-MiniLM-L6-v2"`） |
| `formats` | 否 | `{类型: min_version}` 映射。可选类型：`generic` / `a2a` / `skill`。省略 → 三种全开，均从 `v0.0` 起接受。 |

未列出的类型会被注册端直接拒绝；未知类型 / 版本会被静默丢弃。若 `formats` 正规化后为空集合，返回 400。

**响应：**
```json
{
  "dataset": "my_dataset",
  "embedding_model": "all-MiniLM-L6-v2",
  "formats": { "generic": "v0.0", "a2a": "v0.0", "skill": "v0.0" },
  "status": "created"
}
```

---

### DELETE `/api/datasets/{dataset}`

删除数据集目录及其全部数据，同时清理 ChromaDB 集合并释放缓存的搜索实例。

**响应：**
```json
{ "dataset": "my_dataset", "status": "deleted" }
```

---

### GET `/api/datasets/{dataset}/services`

列出数据集中的服务，支持任意字段筛选 + `brief`/`detail` 投影 + 分页。

> **变更说明（v0.x）**：老版本通过 `mode=browse|admin|full|single|filter` 参数切换 5 种返回形状。本次重构**完全移除 `mode` 参数**，合并为 1 个 list 端点 + 1 个 path-based 单条端点（见下文）。所有响应都是统一的扁平数组（不再有 `{servers, metadata}` 包装），分页元数据通过响应 header 返回。这是一次性破坏性变更。

**查询参数：**

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `fields` | string | `"detail"` | `brief` \| `detail` |
| `size` | int | `-1` | 分页大小，`-1` 返回全部 |
| `page` | int | `1` | 页码（从 1 开始），仅 `size>0` 时生效 |
| `include_leased` | bool | `false` | 是否包含被 reservation lease 锁住的 entry（默认隐藏，使 `list_idle_blank_agents` 自动 race-safe） |
| **其他任意 key** | string | — | 非保留参数全部作为筛选条件（AND，字符串等值） |

保留参数：`fields` / `size` / `page` / `include_leased`（不参与过滤）。

**fields 说明：**

| 投影 | 返回字段 | 用途 |
|------|----------|------|
| `brief` | `id, name, description` | 服务浏览器、轻量列表 |
| `detail` | `id, type, name, description, metadata, source` | 管理面板、SDK `list_agents` |

**响应 — `fields=brief`：**
```json
[
  { "id": "flight_booking", "name": "航班预订", "description": "支持国内外航班预订..." }
]
```

**响应 — `fields=detail`：**
```json
[
  {
    "id": "agent_abc",
    "type": "a2a",
    "name": "_BlankAgent_http://a.example",
    "description": "__BLANK__.",
    "source": "api_config",
    "metadata": { "name": "...", "description": "__BLANK__", "endpoint": "...", "status": "online" }
  }
]
```

**分页响应头**（仅 `size>0` 时设置）：
- `X-Total-Count` — 匹配的总数
- `X-Page` — 当前页码（从 1 开始）
- `X-Total-Pages` — 总页数
- `X-Page-Size` — 当前页大小

**筛选约束**：
- **空过滤条件 → 返回全量**；否则 AND 语义、`str(raw_value) == query_value`
- 字段**必须存在**且值相等才命中
- 匹配的是**原始数据**：对 a2a 是 `entry.agent_card.model_dump(exclude_none=True)`（`description` 是原始未转换值）；对 generic 是 `entry.service_data`；对 skill 是 `entry.skill_data`
- 响应里外层 `description` 是 `build_description(card)` 的输出（a2a 会带句号），但 `metadata.description` 是原始值
- **`status=online` default-online 特例**：当筛选条件含 `status=online` 时，缺 `status` 字段的 entry 也命中（其他字段 / 其他 status 值仍要求字段存在 + 字符串等值）
- 请求示例：`GET /api/datasets/team/services`（全量）、`?description=__BLANK__&status=online`（筛选 idle blank agents）、`?fields=brief&size=20&page=2`（轻量分页浏览）

---

### GET `/api/datasets/{dataset}/services/{service_id}`

按 ID 精确查询单个服务（替换老的 `?mode=single&service_id=X`）。

**响应：**
- a2a / generic：单个 wrapped entry（与 list `fields=detail` 单元素相同形状）
- skill：返回 ZIP 二进制（`Content-Type: application/zip`，`Content-Disposition: attachment`）
- 不存在：`404`，`detail` 含数据集名 + service_id

---

### POST `/api/datasets/{dataset}/services/generic`

注册一个通用服务到指定数据集。

> **自动初始化 dataset**：如果 `{dataset}` 不存在，后端会用默认 `embedding_model="all-MiniLM-L6-v2"` + 全格式 `v0.0` 自动创建（写 `vector_config.json` + `register_config.json`）。需要自定义 embedding 或 formats 时，调用方应**先**调 `POST /api/datasets` 显式创建。事后改 embedding 走 `POST /api/datasets/{dataset}/vector-config`。`POST /services/a2a` 和 `POST /skills` 同样适用。

**请求体：**
```json
{
  "name": "航班预订",
  "description": "支持国内外航班预订与查询",
  "service_id": "flight_booking",
  "url": "https://example.com/api/flight",
  "inputSchema": { "type": "object", "properties": {} },
  "persistent": true
}
```

| 字段 | 必填 | 说明 |
|------|------|------|
| `name` | 是 | 服务名称 |
| `description` | 是 | 服务描述 |
| `service_id` | 否 | 省略则自动生成 |
| `url` | 否 | 服务 URL |
| `inputSchema` | 否 | JSON Schema |
| `persistent` | 否 | `true`（默认）写入 `api_config.json` 持久化；`false` 为会话级 |

**响应：**
```json
{ "service_id": "flight_booking", "dataset": "my_dataset", "status": "registered" }
```

`status` 取值：`"registered"`（新注册） | `"updated"`（更新已有）

---

### POST `/api/datasets/{dataset}/services/a2a`

注册 A2A Agent。支持 URL 抓取或直接传入 Agent Card JSON。

**请求体（URL 模式）：**
```json
{
  "agent_card_url": "https://example.com/.well-known/agent.json",
  "persistent": true
}
```

**请求体（JSON 模式）：**
```json
{
  "agent_card": {
    "name": "翻译助手",
    "description": "多语种翻译 Agent",
    "url": "https://example.com/agent",
    "skills": [{ "name": "translate", "description": "文本翻译" }]
  },
  "persistent": true
}
```

**响应：**
```json
{ "service_id": "translate_agent", "dataset": "my_dataset", "status": "registered" }
```

---

### PUT `/api/datasets/{dataset}/services/{service_id}`

部分字段更新服务（顶层字段 upsert：存在则替换，不存在则追加）。

**请求体：** 任意 `{field: value}` 字典，按类型允许的字段列表校验：

| 类型 | 可改字段 | 备注 |
|------|---------|------|
| `generic` | `name`, `description`, `inputSchema`, `url` | 字段名严格，出现未知键返回 400 |
| `a2a` | Agent Card 任意顶层字段（含 `skills`、`provider`、`capabilities` 等，以及 `extra=allow` 允许的自定义字段） | 顶层替换；想改 `provider.url` 需整段重传 `provider` |
| `skill` | `name`, `description`, `license` | 改 `name` 时 `skills/{name}/` 文件夹会随之重命名；SKILL.md frontmatter 会被相应重写 |

**示例请求：**
```json
{ "description": "更新后的描述", "url": "https://new.example.com" }
```

**响应：**
```json
{
  "service_id": "flight_booking",
  "dataset":    "my_dataset",
  "status":     "updated",
  "changed_fields": ["description", "url"],
  "taxonomy_affected": true
}
```

**行为约定**：

- **不做格式校验**：更新只增不减，原始校验的不变式依然成立；节省一次完整校验。
- 只有 `name` 或 `description` 实际变化时 `taxonomy_affected=true`，并将该数据集的 taxonomy 置为 `STALE`。仅改 `url` / `inputSchema` 等不参与分类 hash 的字段不会影响分类树可用性。
- `changed_fields` 仅列出值真正变化的顶层键（避免同值赋值误报）。
- **A2A + `agent_card_url` 注意**：若 A2A 条目以 URL 注册（以远端抓取为真源），PUT 更新会写入本地缓存快照，但下次 `startup()` 再次抓取 URL 时会用上游数据覆盖。若需持久化编辑，请在注册时不要提供 `agent_card_url`，改以完整 `agent_card` 注册。

**错误响应**：

| 状态码 | 触发 |
|--------|------|
| 400 | `user_config` 来源的服务（需直接编辑 `user_config.json`）/ 未知字段（generic、skill）/ 改名时目标文件夹已存在 |
| 404 | `service_id` 不存在 |

---

### DELETE `/api/datasets/{dataset}/services/{service_id}`

注销指定服务。

**响应（200）：**
```json
{ "service_id": "flight_booking", "status": "deregistered" }
```

`status` 取值：仅 `"deregistered"`。

**错误响应**：

| 状态码 | 触发 |
|--------|------|
| 400 | `user_config` 来源的服务（需直接编辑 `user_config.json`）/ `skill_folder` 来源的服务（需使用 `DELETE /skills/{name}` 端点） |
| 404 | `service_id` 不存在（业务层抛 `RegistryNotFoundError`，路由层映射为 404；不再返回 200 + `status="not_found"`） |

---

## 1.5 预订 / 锁定（Reservation Leases）

> **背景**: 客户场景下多个 teamleader 并发挑选 idle blank agents 时会出现"双重分配"竞态。Lease 机制借鉴 SQS visibility-timeout——内存级、短期（默认 30s）、不持久化、宿主重启即清空。Lease 与 `status` 字段**正交**（lease 是临时锁，status 是长期意图）。

**端点一览：**

| 方法 | 路径 | 说明 |
|------|------|------|
| `POST` | `/api/datasets/{dataset}/reservations` | 预订 N 个匹配 filters 的 service，加锁 ttl_seconds |
| `DELETE` | `/api/datasets/{dataset}/reservations/{holder_id}` | 释放该 holder 的全部 lease（idempotent） |
| `DELETE` | `/api/datasets/{dataset}/reservations/{holder_id}/{service_id}` | 释放单个 sid lease（要求 holder 一致；403 if 不一致） |
| `POST` | `/api/datasets/{dataset}/reservations/{holder_id}/extend` | 续期 holder 的全部 lease；404 if 没有活 lease |
| `DELETE` | `/api/datasets/{dataset}/services/{service_id}/lease` | teammate-self 释放：任意 holder 的 lease 都释放（idempotent） |

**Lease 与 `GET /services` 的交互**：默认 `include_leased=false`，被 lease 的 entry 自动从结果中过滤掉。这是 `list_idle_blank_agents` 自动 race-safe 的关键。

---

### POST `/api/datasets/{dataset}/reservations`

预订匹配 filters 的最多 N 个 service。filter+claim 在内部 `_lock` 下原子完成（TOCTOU-safe）。如果可用数 < N，返回实际预订数（不报错）。

**请求体：**
```json
{
  "filters": { "description": "__BLANK__", "status": "online" },
  "n": 1,
  "ttl_seconds": 30,
  "holder_id": null
}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| `filters` | dict | 同 `GET /services` 的 filter 语义（含 default-online 特例） |
| `n` | int >= 0 | 最多预订数；实际可能小于此 |
| `ttl_seconds` | int >= 1 | lease 持续时长（秒）；默认 30 |
| `holder_id` | string \| null | null 时后端自动生成 `holder_<uuid>` |

**响应（200）：**
```json
{
  "holder_id": "holder_abc123",
  "ttl_seconds": 30,
  "expires_at_unix": 1729999999.5,
  "reservations": [
    { "id": "agent_1", "type": "a2a", "name": "...", "description": "__BLANK__.", "metadata": { ... } }
  ]
}
```

`reservations` 形状与 `GET /services?fields=detail` 单元素相同。

---

### DELETE `/api/datasets/{dataset}/reservations/{holder_id}`

释放该 holder 在此 dataset 的全部 lease。**幂等**：未持有任何 lease 也返回 200。

**响应：** `{ "released": ["sid_1", "sid_2", ...] }`

---

### DELETE `/api/datasets/{dataset}/reservations/{holder_id}/{service_id}`

释放该 holder 持有的单个 sid lease。

**响应：**
- `{ "released": ["agent_1"] }` — 成功释放
- `{ "released": [] }` — sid 不在 lease 表中（idempotent）
- `403` — sid 被另一个 holder 持有（不允许释放他人的 lease）

---

### POST `/api/datasets/{dataset}/reservations/{holder_id}/extend`

延长该 holder 全部 lease 的 TTL。

**请求体：** `{ "ttl_seconds": 60 }`

**响应（200）：** `{ "expires_at_unix": 1729999999.5 }`

**404**：holder 没有任何活 lease（已过期或从未持有）。设计意图：不允许"复活"已过期的 lease——避免悄悄延长丢失了的工作窗口。

---

### DELETE `/api/datasets/{dataset}/services/{service_id}/lease`

**teammate-self** 释放路径：释放任意 holder 在 sid 上的 lease。授权由 SDK 层的 `_owned` 检查兜底（agent 只能释放自己注册的 sid 的 lease）。

**响应（200）：**
- `{ "released": true,  "prev_holder_id": "holder_abc123" }`
- `{ "released": false, "prev_holder_id": null }` — sid 没有 lease（idempotent）

**为什么需要这个端点**: leader 把 lease 信息通过 HTTP 注册到 backend，而不是 P2P 告诉 teammate；teammate 不知道 holder_id，所以无法通过 holder-routed 端点释放。`replace_agent_card` 的 SDK 自动钩子调用此端点。

---

### POST `/api/datasets/{dataset}/skills`

上传 Skill 文件夹（ZIP 格式）。ZIP 必须包含 `SKILL.md`（根目录或单层子目录内），frontmatter 需含 `name` 和 `description` 字段。

**请求：** `Content-Type: multipart/form-data`，字段名 `file`。

**响应：**
```json
{ "name": "algorithmic-art", "dataset": "default", "service_id": "skill_xxxx", "status": "registered" }
```

`status` 取值：`"registered"` | `"updated"`（同名覆盖）

---

### DELETE `/api/datasets/{dataset}/skills/{name}`

删除 Skill 文件夹及其注册条目。

**响应：**
```json
{ "name": "algorithmic-art", "dataset": "default", "service_id": "skill_xxxx", "status": "deleted" }
```

`status` 取值：`"deleted"` | `"not_found"`

---

### GET `/api/datasets/{dataset}/skills/{name}/download`

将 Skill 文件夹打包为 ZIP 下载。

**响应：** `Content-Type: application/zip`，附件名 `{name}.zip`。

---

### GET `/api/datasets/{dataset}/taxonomy`

获取分类树结构，用于前端 D3.js 可视化。

**响应：** 完整的分类树 JSON（`taxonomy.json` 格式），包含 `root`、`categories`、`build_status` 字段。

若分类树尚未构建，返回 404。

---

### GET `/api/datasets/{dataset}/default-queries`

获取示例查询（随机子集），用于前端输入框建议。

**查询参数：**

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `count` | int | `5` | 返回条数 |

**响应：**
```json
[
  { "query": "帮我预订下周五的航班", "query_en": "Book a flight for next Friday" }
]
```

---

### GET `/api/datasets/embedding-models`

返回系统支持的嵌入模型列表。

**响应：**
```json
{
  "models": {
    "all-MiniLM-L6-v2": { "dim": 384, "description": "..." },
    "bge-small-zh-v1.5": { "dim": 512, "description": "..." },
    "text-embedding-3-small": { "dim": 1536, "description": "..." }
  }
}
```

---

### GET `/api/datasets/{dataset}/vector-config`

获取数据集当前的嵌入（向量）配置。

**响应：**
```json
{ "dataset": "ToolRet_clean", "embedding_model": "all-MiniLM-L6-v2", "embedding_dim": 384 }
```

---

### POST `/api/datasets/{dataset}/vector-config`

设置数据集的嵌入模型，保存后自动触发向量索引后台重建。

**请求体：**
```json
{ "embedding_model": "bge-small-zh-v1.5" }
```

**响应：**
```json
{
  "dataset": "ToolRet_clean",
  "embedding_model": "bge-small-zh-v1.5",
  "embedding_dim": 512,
  "message": "配置已保存，向量索引将在后台重建"
}
```

若模型名未知且未提供 `embedding_dim`，返回 400。

---

### GET `/api/datasets/{dataset}/register-config`

获取该数据集允许的注册格式（类型及每种类型的最老协议版本）。

**响应：**
```json
{
  "dataset": "my_dataset",
  "formats": { "generic": "v0.0", "a2a": "v0.0", "skill": "v0.0" }
}
```

文件缺失时返回全量默认值（三种类型全开，均为 `v0.0`）。

---

### POST `/api/datasets/{dataset}/register-config`

覆盖式替换允许的注册格式（非增量合并）。

**请求体：**
```json
{
  "formats": {
    "generic": "v0.0",
    "a2a":     "v1.0"
  }
}
```

| 字段 | 必填 | 说明 |
|------|------|------|
| `formats` | 是 | `{类型: min_version}` 映射，也可写成 `{"a2a": {"min_version": "v1.0"}}` |

- 未知类型 / 版本被静默丢弃。
- 正规化后为空 → 400。
- 未被列出的类型之后不再接受注册；已注册的服务不会自动清理，下次启动才重新校验并剔除不合规条目。

**响应：**
```json
{ "dataset": "my_dataset", "formats": { "generic": "v0.0", "a2a": "v1.0" } }
```

**注册校验规则**：
- 注册请求的 `type`（由 endpoint 决定）必须在 `formats` 中，否则 400。
- 对应类型的 payload 从 `min_version` 起，依类型声明的 `SUPPORTED_VERSIONS` **从老到新**依次尝试；任一版本通过则整体通过。
- 所有 `v0.0` 只校验 `name` + `description`。更严格的版本（如 A2A v1.0）追加完整字段检查。

---

## 2. 构建路由 `/api/datasets/{dataset}/build`

### 端点一览

| 方法 | 路径 | 说明 |
|------|------|------|
| `POST` | `/api/datasets/{dataset}/build` | 触发分类树构建 |
| `GET` | `/api/datasets/{dataset}/build/status` | 查询构建状态 |
| `DELETE` | `/api/datasets/{dataset}/build` | 取消构建 |
| `GET` | `/api/datasets/{dataset}/build/stream` | SSE 实时日志流 |

---

### POST `/api/datasets/{dataset}/build`

触发后台分类树构建任务。

**请求体（`BuildRequest`，所有字段均可选）：**
```json
{
  "resume": "yes",
  "workers": 20,
  "generic_ratio": 0.333,
  "delete_threshold": 2,
  "max_depth": 3
}
```

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `resume` | string | `"no"` | `"no"` 完整重建 / `"yes"` 断点续传 / `"keyword"` 复用关键词重建 |
| `workers` | int | — | 并发 LLM 线程数 |
| `generic_ratio` | float | — | 通用类别比例阈值 |
| `delete_threshold` | int | — | 类别最小服务数（低于则删除） |
| `max_depth` | int | — | 最大分类树深度 |
| `max_service_size` | int | — | 叶节点最大服务数 |
| `max_categories_size` | int | — | 同级最大类别数 |
| `min_leaf_size` | int | — | 叶节点最小服务数 |
| `enable_cross_domain` | bool | — | 是否启用跨域分配 |
| `log_level` | string | — | 构建日志捕获级别（`"DEBUG"` / `"INFO"` / `"WARNING"` / `"ERROR"`），未指定时跟随系统默认级别 |

**响应：**
```json
{ "dataset": "ToolRet_clean", "status": "started", "message": "构建已启动" }
```

409 若该数据集已有构建在运行。

---

### GET `/api/datasets/{dataset}/build/status`

查询当前构建状态（一次性 HTTP 请求，非流式）。

**响应：**
```json
{
  "dataset": "ToolRet_clean",
  "status": "running",
  "message": "构建中，请稍候...",
  "started_at": 1711430400.0,
  "finished_at": null,
  "logs": ["10:00:01  ███░░ 12.5% [23/184] keywords", "..."]
}
```

`status` 取值：`"idle"` | `"running"` | `"done"` | `"cancelled"` | `"error"`

---

### DELETE `/api/datasets/{dataset}/build`

取消正在运行的构建任务。

向构建线程发送取消信号（`threading.Event`），构建在下一个阶段边界处停止。立即向 SSE 订阅者推送 `status: cancelled` 事件。

**响应：**
```json
{ "dataset": "ToolRet_clean", "status": "cancelled", "message": "构建已取消" }
```

409 若无运行中的构建。

---

### GET `/api/datasets/{dataset}/build/stream`

**Server-Sent Events 流**，实时推送构建日志。

`Content-Type: text/event-stream`，每个事件为一行 `data: <JSON>\n\n`。

**连接行为：**
1. 连接时先回放 `logs` 中已有的历史日志
2. 若构建未在运行，发送当前状态后关闭
3. 若构建正在运行，订阅后续实时日志，直到构建结束或客户端断开

**事件格式：**
```
data: {"type": "log",    "message": "10:00:05  ████░░ 23.1% [425/1839] assigned"}

data: {"type": "status", "status": "done",      "message": "分类树构建完成"}
data: {"type": "status", "status": "error",     "message": "service.json not found"}
data: {"type": "status", "status": "cancelled", "message": "构建已取消"}
```

收到 `type: "status"` 事件后流关闭。构建运行中每秒无新事件时发送 `: keepalive`（SSE 注释，浏览器忽略），防止连接超时。

---

## 3. 搜索路由 `/api/search`

### 端点一览

| 方法 | 路径 | 说明 |
|------|------|------|
| `POST` | `/api/search` | 同步搜索 |
| WebSocket | `/api/search/ws` | A2X 流式搜索 |
| `POST` | `/api/search/judge` | LLM 相关性判断 |

---

### POST `/api/search`

同步搜索，结果一次性返回。适用于所有搜索方法。

**请求体：**
```json
{
  "query": "预订北京到东京的航班",
  "method": "vector_5",
  "dataset": "ToolRet_clean",
  "top_k": 5
}
```

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `query` | string | — | 搜索查询（必填） |
| `method` | string | — | 搜索方法（必填） |
| `dataset` | string | `"ToolRet_clean"` | 数据集名称 |
| `top_k` | int | `10` | 返回数量（主要用于 vector 方法） |

`method` 取值：`vector_5` | `vector_10` | `traditional` | `a2x_get_all` | `a2x_get_important` | `a2x_get_one`

**响应：**
```json
{
  "results": [
    { "id": "flight_booking", "name": "航班预订", "description": "..." }
  ],
  "stats": { "llm_calls": 3, "total_tokens": 1520 },
  "elapsed_time": 0.32
}
```

---

### WebSocket `/api/search/ws`

A2X 流式搜索，实时返回每一步分类导航过程，驱动前端树动画。非 A2X 方法也支持，直接返回最终结果。

**客户端发送（连接建立后）：**
```json
{ "query": "预订航班", "method": "a2x_get_all", "dataset": "ToolRet_clean", "top_k": 10 }
```

**服务端推送消息序列：**
```json
// 每个导航步骤（重复多次，仅 A2X 方法）
{ "type": "step", "data": { "parent_id": "交通出行", "selected": ["航班预订", "机票查询"], "pruned": ["酒店预订"] } }

// 最终结果（一次）
{ "type": "result", "data": { "results": [...], "stats": {...}, "elapsed_time": 2.14 } }

// 错误（可选）
{ "type": "error", "message": "LLM call failed: ..." }
```

搜索结束后服务端主动关闭连接。

---

### POST `/api/search/judge`

LLM 相关性判断，用于对比图中验证检索结果质量。

**请求体：**
```json
{
  "query": "预订北京到东京的航班",
  "services": [
    { "id": "flight_booking", "name": "航班预订", "description": "..." }
  ]
}
```

**响应：**
```json
{
  "results": [
    { "service_id": "flight_booking", "relevant": true }
  ]
}
```

---

## 4. 提供商路由 `/api/providers`

### 端点一览

| 方法 | 路径 | 说明 |
|------|------|------|
| `GET` | `/api/providers` | 列出 LLM 提供商 |
| `POST` | `/api/providers/{name}` | 切换提供商 |

---

### GET `/api/providers`

列出所有可用 LLM 提供商及当前激活的提供商。提供商配置来源于 `llm_apikey.json`。

**响应：**
```json
{
  "providers": [
    { "name": "deepseek", "model": "deepseek-chat" },
    { "name": "openai",   "model": "gpt-4o" }
  ],
  "current": "deepseek"
}
```

---

### POST `/api/providers/{name}`

切换 LLM 提供商。修改 `llm_apikey.json` 中的排序（将目标提供商置顶），同时重置所有缓存的 A2X 搜索实例。

**路径参数：** `name` — 提供商名称（须与 `llm_apikey.json` 中的 `name` 一致）

**响应（成功）：**
```json
{ "status": "ok", "current": "openai" }
```

**响应（失败 — 提供商不存在）：**
```json
{ "error": "Unknown provider: xxx", "valid": ["deepseek", "openai"] }
```

---

## 5. 应用级端点

### GET `/api/warmup-status`

预热状态查询，前端加载屏幕轮询此端点直到 `ready: true`。

**响应：**
```json
{ "stage": "taxonomy", "progress": 0.6, "ready": false }
```
```json
{ "stage": "done", "progress": 1.0, "ready": true }
```

---

## 并发构建隔离

多个数据集可同时构建，互不干扰：

- `_build_jobs` / `_log_subs` / `_cancel_flags` 均以 `dataset` 为 key 独立存储
- `_LogCapture` handler 按**线程 ID** 过滤日志记录，防止两个构建线程共享同一 `src.a2x` logger 时日志串流；日志捕获级别由请求参数 `log_level` 控制，未指定时跟随 `src.a2x` logger 的系统默认级别
- `_push_to_subs(dataset, event)` 只向该数据集的订阅者队列推送
