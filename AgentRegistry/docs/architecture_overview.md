# A2X Registry 整体架构

本文档面向想要快速建立全局认识的开发者：先看这一页，再去对应的模块设计文档深入。

各模块详细设计：
- [register_design.md](register_design.md) — 注册模块
- [build_design.md](build_design.md) — A2X 分类树自动构建
- [search_design.md](search_design.md) — A2X 搜索算法
- [incremental_design.md](incremental_design.md) — 增量构建
- [auth_design.md](auth_design.md) — 鉴权模块（静态 API Key + 三档角色 + namespace 作用域，**默认关闭，向前兼容**）
- [heartbeat_design.md](heartbeat_design.md) — 心跳保活模块（per-namespace opt-in 租约 + 软驱逐 / 硬删两段式，**默认关闭，向前兼容**）
- [cluster_design.md](cluster_design.md) — 分布式同步模块（多注册中心 gossip 复制 + 失活驱逐，**默认关闭，向前兼容**）；部署见 [README_forDistributed.md](../README_forDistributed.md)
- [backend_api.md](backend_api.md) — 后端 HTTP API
- [frontend_design.md](frontend_design.md) — Web UI
- [a2x_design.md](a2x_design.md) — 系统整体视图（更深入）

## 1. 模块布局

```
a2x_registry/                      # pip 包根
├── common/                        # 横切层：models / errors / paths / naming
│   ├── llm_client.py              #   多 provider LLM 客户端（requests）
│   ├── feature_flags.py           #   has() / require() — 检测可选 extras
│   ├── auth_context.py            #   中立 AuthContext dataclass（register/auth 共用）
│   ├── lease.py                   #   泛型 LeaseTable[K] 租约状态机（heartbeat / cluster 共用）
│   ├── atomic.py                  #   跨平台原子 JSON 写
│   └── errors.py                  #   FeatureNotInstalledError 等
├── register/                      # 注册中心核心
│   ├── service.py                 #   RegistryService（多数据集 CRUD + 预订锁）
│   ├── store.py                   #   service.json / api_config.json 持久化
│   ├── validation.py              #   Generic / A2A / Skill 校验
│   └── agent_card.py              #   AgentCard 抓取与描述生成
├── auth/                          # 鉴权模块（默认关闭；不 import register/）
│   ├── store.py                   #   AuthStore：principal/key 文件 IO + 索引
│   ├── deps.py                    #   FastAPI Depends（authorize / require_admin / ...）
│   ├── router.py                  #   /api/auth/* 端点
│   ├── cli.py                     #   `a2x-registry auth init / reset-admin`
│   └── auth_data/                 #   运行时数据，.gitignore，不进 wheel
├── heartbeat/                     # 心跳保活（per-namespace opt-in；不 import register/）
│   ├── store.py                   #   HeartbeatStore：复用 common/lease 的 LeaseTable
│   ├── sweeper.py                 #   后台 daemon 线程，mark unhealthy / 硬删
│   ├── router.py                  #   /api/datasets/{ds}/services/{sid}/heartbeat
│   └── system_ctx.py              #   合成 admin context 给 sweeper 走 deregister
├── cluster/                       # 分布式同步（opt-in，需 cluster init；不 import register/ 业务）
│   ├── store.py                   #   ClusterStore：foreign overlay + 会话表 + 全连接复制/HOLD 驱逐 + 并发广播池
│   ├── membership.py              #   声明式成员控制面：roster(LWW) + set add/remove + 退老群 + 自动全连接
│   ├── merkle.py                  #   分桶 Merkle 反熵摘要（规模化）
│   ├── envelope.py                #   SyncEnvelope + LWW 版本判新
│   ├── state.py                   #   cluster_state.json：node_id / 版本 / 墓碑 / cluster_id / 名册
│   ├── peer.py / auth_handshake.py #  会话模型 / 逐 namespace 握手鉴权（复用 auth）
│   ├── sweepers.py                #   后台守护线程：成员对账 + 反熵 + GC / keepalive·HOLD
│   ├── transport.py / router.py    #  httpx 连接池传输 / RESTful /api/cluster/*
│   └── cli.py                     #   `a2x-registry cluster init/set add|remove|show/status`
├── backend/                       # FastAPI 应用
│   ├── app.py                     #   入口、CORS、503 异常 handler
│   ├── startup.py                 #   warmup（按 has("vector") 分阶段；加载 AuthStore）
│   ├── routers/                   #   dataset / search / build / provider
│   └── services/                  #   search_service / taxonomy_service
├── a2x/                           # 纯 LLM，lite 可用
│   ├── build/                     #   分类树构建（需 LLM 配置）
│   └── search/                    #   两阶段 LLM 递归检索
├── vector/                        # ⚠️ 需 [vector] extras
│   ├── utils/embedding_constants.py  # 0 重依赖，常量在此对外暴露
│   ├── utils/{embedding,chroma_store,metrics}.py
│   ├── search/, build/            #   ChromaDB 向量索引与检索
│   └── evaluation/                #   评估 CLI（需额外 [evaluation]）
└── traditional/                   # 纯 LLM，lite 可用
    └── search/                    #   MCP 全上下文基线
```

> 仓库还包含 `ui/`（React + Vite Web UI，仅源码克隆可用）、`docs/`、`results/`、`database/`（git submodule，演示数据），这些不在 pip wheel 内。

## 2. 模块职责与主要对外接口

### 2.1 `common/` — 横切层，所有模块依赖

| 接口 | 用途 |
|---|---|
| `LLMClient(...).call(messages)` | 统一的 LLM API 调用（DeepSeek / OpenAI 兼容），多 provider 故障转移 |
| `feature_flags.has("vector")` / `.require("vector")` | 探测 `[vector]` / `[evaluation]` extras 是否安装 |
| `paths.database_dir()` / `llm_apikey_path()` | 解析 `A2X_REGISTRY_HOME` / `~/.a2x_registry/` / 当前目录 |
| `errors.{FeatureNotInstalledError, VectorSearchUnavailableError, LLMNotConfiguredError}` | 三类用户可执行错误（消息可直接打印） |

### 2.2 `register/` — 注册中心业务逻辑（lite 必备）

| 接口 | 调用方 |
|---|---|
| `RegistryService.startup()` | 后端启动；加载所有数据集 → 返回 `{dataset: TaxonomyState}` |
| `register_a2a()` / `register_generic()` / `register_skill()` | 后端 HTTP / CLI；新增服务并持久化 |
| `update_service()` / `deregister()` | 局部字段更新 / 注销 |
| `list_services()` / `list_entries()` / `get_entry()` | 浏览与按字段过滤 |
| `reserve_services()` / `extend_reservation()` / `release_reservation()` / `release_lease_by_sid()` | 预订锁（in-memory lease，重启丢失） |
| `set_on_service_changed(callback)` | 服务变更回调（lite 不挂） |
| `create_dataset()` / `delete_dataset()` / `set_vector_config()` / `set_register_config()` | 数据集管理 |

输出：`database/{dataset}/service.json`、`api_config.json`、`vector_config.json`、`register_config.json`。SDK 全部走这一层。

### 2.3 `backend/` — FastAPI 应用

| 路由前缀 | 文件 | lite 可用？ |
|---|---|---|
| `/api/datasets/*` | `routers/dataset.py` | ✅ 全部 |
| `/api/datasets/{ds}/build/{status,stream}`（只读） | `routers/build.py` | ✅ |
| `POST /api/datasets/{ds}/build`（A2X 分类构建，纯 LLM） | `routers/build.py` | ✅（需配 LLM） |
| `POST /api/search`（按 method 分发） | `routers/search.py` | a2x_*/traditional ✅；vector ❌ 503 |
| `POST /api/search/judge`（LLM 判定相关性） | `routers/search.py` | ✅（需配 LLM） |
| `/api/search/ws`（按 method 分发） | `routers/search.py` | a2x_*/traditional ✅；vector ❌ 503 |
| `/api/providers/*` | `routers/provider.py` | ✅（仅文件读写） |
| `/api/warmup-status` | `app.py` | ✅ |

**关键：only `method=vector` 真正需要 `[vector]` extras**（numpy + chromadb + sentence-transformers）。A2X 和 Traditional 都是纯 LLM 算法，lite 安装直接可用。503 响应 body：`{feature, extras, detail}`，detail 含可执行的 `pip install` 命令。详见 [backend_api.md](backend_api.md) 的"安装模式与可用性"小节。

辅助层：

- `services/search_service.SearchService` — A2X / vector / traditional 三种检索的统一外观，按需 lazy import 重依赖；`schedule_vector_sync` / `purge_dataset` 在 lite 下早退。
- `services/taxonomy_service.get_taxonomy_tree(dataset)` — 仅读 `taxonomy.json`，lite 可用。
- `startup.run_warmup` — 阶段 1-2（数据集加载、taxonomy 缓存）始终跑；阶段 3-6（A2X 引擎、Chroma 清理、向量同步、嵌入预热）在 `feature_flags.has("vector")` 为真时才跑。

### 2.4 `a2x/` — A2X 分类构建与递归搜索（需 `[vector]`）

| 接口 | 输入 | 输出 |
|---|---|---|
| `a2x.build.taxonomy_builder.TaxonomyBuilder.build(resume)` | `service.json` + `AutoHierarchicalConfig` | `taxonomy.json` + `class.json` |
| `a2x.search.a2x_search.A2XSearch.search(query, mode)` | 查询文本 + `get_one`/`get_all`/`get_important` | `(List[SearchResult], SearchStats)` |
| `a2x.search.a2x_search.A2XSearch.search_stream(...)` | 同上 | 生成器，逐步 yield 导航步骤 |

### 2.5 `vector/` — ChromaDB 向量检索基线（需 `[vector]`）

| 接口 | 说明 |
|---|---|
| `vector.utils.embedding_constants.{DEFAULT_EMBEDDING_MODEL, EMBEDDING_MODELS}` | **0 重依赖**，register/backend 在 lite 下也读这里 |
| `vector.utils.embedding.EmbeddingModel(name)` / `vector.utils.chroma_store.ChromaStore(...)` | 经 PEP 562 `__getattr__` 懒加载，仍可 `from a2x_registry.vector.utils import EmbeddingModel` |
| `vector.search.vector_search.VectorSearch.search(query, top_k)` | top-K 向量检索 |
| `vector.build.index_builder.IndexBuilder` | 从 service.json 增量同步 ChromaDB collection |

### 2.6 `traditional/` — MCP 全上下文基线（需 `[vector]`）

| 接口 | 说明 |
|---|---|
| `traditional.search.traditional_search.TraditionalSearch.search(query)` | 把全部服务塞 LLM context 由 LLM 选择 |

### 2.7 `auth/` — 静态 API Key 鉴权（默认关闭）

**两层 opt-in**：注册中心未跑 `a2x-registry auth init` 时，所有 `/api/auth/*` 返回 404、所有 namespace 维持完全匿名（与无鉴权代码 byte-equal）；每个 namespace 通过 `auth_required=true` 才进入鉴权路径。

| 接口 | 说明 |
|---|---|
| `auth.cli.main(["init"])` | 首次 bootstrap，生成第一个 admin principal + key |
| `auth.store.AuthStore.load_or_none()` | 启动时由 `backend/startup.py` 调用；未 bootstrap 返回 None |
| `auth.deps.authorize` | 单一 FastAPI Depends，按 `request.path_params["dataset"]` 自动分流匿名 / 严格路径 |
| `auth.deps.require_admin` / `require_admin_or_anon` / `require_admin_strict` | 分别对应"始终管理员"、"管理员或匿名 ns"、"管理员但跳过 namespace 网关"三种场景 |
| `auth.router` — `/api/auth/{whoami,principals,keys}` | 自管 API（CRUD principal、CRUD 自己的 key） |
| `common.auth_context.AuthContext` | `register/` 与 `auth/` 之间的中立握手类型（`register/` 永不 import `auth/`） |

三档角色：**admin** 全局；**provider** 绑定 namespace 列表，可注册并改自己 owned 的服务；**user** 绑定 namespace 列表，只读 + 预约。owner / holder 字段服务端强制写入，客户端无法伪造。完整鉴权矩阵与不变式见 [auth_design.md](auth_design.md)。

### 2.8 `heartbeat/` — 服务心跳保活 / 租约（默认关闭）

**Per-namespace opt-in**：注册中心始终加载心跳模块，但 namespace 必须显式 POST `lease_config {enabled:true, min_ttl, max_ttl, grace_period}` 才会接受 `lease_ttl`。客户端注册时也要显式带 `lease_ttl` —— 4 角矩阵：

| ns / client | 不带 ttl | 带 ttl |
|---|---|---|
| 未启用 | ✅ 永久（向前兼容） | 400 `heartbeat_not_supported` |
| 已启用 | 400 `ttl_required` | ✅ in-range / 400 `ttl_out_of_range` |

| 接口 | 说明 |
|---|---|
| `heartbeat.store.HeartbeatStore` | 内存 lease 表 + `validate/install/heartbeat/revoke/sweep_tick` |
| `heartbeat.sweeper.HeartbeatSweeper` | 后台 daemon 线程，TTL 过期 → mark UNHEALTHY；grace 过期 → 走 `RegistryService.deregister(caller=SYSTEM_CTX)` |
| `heartbeat.router` — POST `/services/{sid}/heartbeat` | 续约（可顺带 status piggyback） |
| `heartbeat.router` — DELETE `/services/{sid}/heartbeat` | 软撤销 (mark unhealthy) 或 `{permanent:true}` 硬删 |
| `RegistryService.set_unhealthy_check(callback)` | 注入点 —— `register/` 不 import `heartbeat/`，靠 callback 通信 |

三档租约状态：HEALTHY → UNHEALTHY（TTL 过期，软驱逐 + grace window 可恢复）→ HARD-DELETED（grace 过期）。`list_services` 默认过滤 UNHEALTHY，`?include_unhealthy=true` 可查全部。完整状态机 / 矩阵 / 重启恢复设计见 [heartbeat_design.md](heartbeat_design.md)。

### 2.9 `cluster/` — 分布式同步（默认关闭）

**opt-in**：注册中心默认单机；执行 `a2x-registry cluster init` 生成 `cluster_state.json` 后才启用。未启用时所有 `/api/cluster/*` 返回 404、读写与单机 byte-equal。启用后用户在任一节点 `cluster set add` 声明成员，系统据名册自动组成**全连接**集群同步注册表（**AP / 最终一致**，直接广播 + LWW），查询任一节点即得全网节点的服务；`set remove` 确定性移除，节点失联则靠直链 HOLD 失活删除其数据。

| 接口 | 说明 |
|---|---|
| `cluster.store.ClusterStore` | 核心：foreign overlay（只读、仅内存）+ 会话表 + 全连接复制 / HOLD 驱逐 + 并发广播池 |
| `cluster.membership.MembershipStore` | 声明式成员控制面：roster(LWW 叠加层) + set add/remove + 退老群 + roster→会话对账 + 名册增量反熵 |
| `cluster.router` — `/api/cluster/{set/*,join,evicted,leave,sessions,merkle,digest,pulls,updates,keepalives,peers,state}` | RESTful 端点（同步 `def`，阻塞 RPC 在线程池跑，避免互连死锁）；未初始化 404 |
| `cluster.cli` — `a2x-registry cluster {init,set add\|remove\|show,status}` | 用户主用入口；`add-peer/rm-peer` 降为内部原语 |
| `RegistryService.set_on_mutation(callback)` | 注入点 —— `register/` 不 import `cluster/`，本地 CRUD 经回调推送增量 |
| 数据集 `GET /services`、`GET /services/{id}` | 读路径合并 foreign 副本（命名空间化 id `origin_id:sid` + `source=cluster`）；成员记录不入此路径 |

写入 origin-only（外部副本只读）、版本 `(updated_at_ms, node_id)` LWW。**全连接、直接广播、不转发**：来源直发所有 peer，入站只按 LWW 落库，天然无环。成员控制面单独维护一条 origin-only+LWW 的 membership 记录（独立 roster 叠加层），用 `set add`(含 bootstrap 直推)/`set remove`(确定性墓碑)/退老群驱动全连接，跨来源墓碑用 `next_version_after` 保证 LWW 必胜。失活统一走直链 keepalive/HOLD（默认 30s）+ 驱逐后抑制冷却。规模化（~1000 节点）：并发广播池 + 连接池 + 分桶 Merkle 反熵 + 可调心跳周期。完整设计 / 时序图 / 类图见 [cluster_design.md](cluster_design.md)，部署见 [README_forDistributed.md](../README_forDistributed.md)。

## 3. 主要数据流

```
register/ ──┐
            │ service.json + 变更回调
            ▼
   ┌──── search_service ────┐
   │                        │
   ▼                        ▼
a2x/build → taxonomy.json   vector/build → ChromaDB
   │                        │
   ▼                        ▼
a2x/search ←──┬──→ vector/search ←──┬──→ traditional/search
              │                     │
              └──── /api/search ────┘
                       (后端 router)
```

- 注册模块是真相之源：service.json 由它负责生成。
- search_service 监听 register 的 `_on_service_changed` 回调，在 [vector] 装好时异步同步 Chroma 索引；lite 模式下回调不挂。
- 搜索路由在请求入口跑 `feature_flags.require("vector")`：未装则 503。

## 4. 安装模式与依赖

详见 [README.md](../README.md) 与 [README_forAgentTeam.md](../README_forAgentTeam.md)。简表：

| 安装命令 | 包含 | 可用范围 |
|---|---|---|
| `pip install a2x-registry` | requests / httpx / fastapi / pydantic / python-multipart / uvicorn[standard] | 注册中心 + SDK 全部接口 + 分布式同步（Agent Team 默认） |
| `pip install 'a2x-registry[vector]'` | + numpy / sentence-transformers / chromadb | + A2X 分类构建 / 向量检索 / traditional 检索 |
| `pip install 'a2x-registry[evaluation]'` | + tqdm | + `a2x-evaluate-*` CLI 进度条 |
| `pip install 'a2x-registry[full]'` | 上述全部 | 等价 ≤0.1.5 默认安装 |

`feature_flags.require()` 入口拦截使用 `importlib.util.find_spec`（不缓存），所以 `pip install [vector]` 后**重启 backend** 即可生效，无需改代码。
