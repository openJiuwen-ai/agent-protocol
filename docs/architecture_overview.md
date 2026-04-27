# A2X Registry 整体架构

本文档面向想要快速建立全局认识的开发者：先看这一页，再去对应的模块设计文档深入。

各模块详细设计：
- [register_design.md](register_design.md) — 注册模块
- [build_design.md](build_design.md) — A2X 分类树自动构建
- [search_design.md](search_design.md) — A2X 搜索算法
- [incremental_design.md](incremental_design.md) — 增量构建
- [backend_api.md](backend_api.md) — 后端 HTTP API
- [frontend_design.md](frontend_design.md) — Web UI
- [a2x_design.md](a2x_design.md) — 系统整体视图（更深入）

## 1. 模块布局

```
a2x_registry/                      # pip 包根
├── common/                        # 横切层：models / errors / paths / naming
│   ├── llm_client.py              #   多 provider LLM 客户端（requests）
│   ├── feature_flags.py           #   has() / require() — 检测可选 extras
│   └── errors.py                  #   FeatureNotInstalledError 等
├── register/                      # 注册中心核心
│   ├── service.py                 #   RegistryService（多数据集 CRUD + 预订锁）
│   ├── store.py                   #   service.json / api_config.json 持久化
│   ├── validation.py              #   Generic / A2A / Skill 校验
│   └── agent_card.py              #   AgentCard 抓取与描述生成
├── backend/                       # FastAPI 应用
│   ├── app.py                     #   入口、CORS、503 异常 handler
│   ├── startup.py                 #   warmup（按 has("vector") 分阶段）
│   ├── routers/                   #   dataset / search / build / provider
│   └── services/                  #   search_service / taxonomy_service
├── a2x/                           # ⚠️ 需 [vector] extras
│   ├── build/                     #   分类树构建
│   └── search/                    #   两阶段 LLM 递归检索
├── vector/                        # ⚠️ 需 [vector] extras
│   ├── utils/embedding_constants.py  # 0 重依赖，常量在此对外暴露
│   ├── utils/{embedding,chroma_store,metrics}.py
│   ├── search/, build/            #   ChromaDB 向量索引与检索
│   └── evaluation/                #   评估 CLI（需额外 [evaluation]）
└── traditional/                   # ⚠️ 需 [vector] extras
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
| `POST /api/datasets/{ds}/build`（触发） | `routers/build.py` | ❌ 503 |
| `/api/search/*`（含 WebSocket） | `routers/search.py` | ❌ 503 |
| `/api/providers/*` | `routers/provider.py` | ✅（仅文件读写） |
| `/api/warmup-status` | `app.py` | ✅ |

503 响应 body：`{feature, extras, detail}`，detail 含可执行的 `pip install` 命令。详见 [backend_api.md](backend_api.md) 的"安装模式与可用性"小节。

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
| `pip install a2x-registry` | requests / fastapi / pydantic / python-multipart / uvicorn[standard] | 注册中心 + SDK 全部接口（Agent Team 默认） |
| `pip install 'a2x-registry[vector]'` | + numpy / sentence-transformers / chromadb | + A2X 分类构建 / 向量检索 / traditional 检索 |
| `pip install 'a2x-registry[evaluation]'` | + tqdm | + `a2x-evaluate-*` CLI 进度条 |
| `pip install 'a2x-registry[full]'` | 上述全部 | 等价 ≤0.1.5 默认安装 |

`feature_flags.require()` 入口拦截使用 `importlib.util.find_spec`（不缓存），所以 `pip install [vector]` 后**重启 backend** 即可生效，无需改代码。
