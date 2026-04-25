# A2X 系统设计文档

本文档包含系统整体视图。各模块详细设计见：
- 注册模块：[register_design.md](register_design.md)
- 构建模块：[build_design.md](build_design.md)
- 搜索模块：[search_design.md](search_design.md)
- 增量构建模块：[incremental_design.md](incremental_design.md)
- 前端：[frontend_design.md](frontend_design.md)

---

## 系统整体视图

### 1. 流程逻辑说明

A2X Registry 由以下模块组成，围绕核心数据结构 **分类树** 和 **服务注册表** 协同工作：

| 模块 | 职责 | 状态 |
|------|------|------|
| **注册模块** (`src/register/`) | 服务注册/注销/查询，管理三类服务（Generic + A2A + Skill），输出 service.json | ✅ |
| **构建模块** (`src/a2x/build/`) | 从服务列表自动构建层次化分类树 | ✅ |
| **搜索模块** (`src/a2x/search/`) | 基于分类树执行两阶段 LLM 递归检索 | ✅ |
| **增量构建** (`src/a2x/incremental/`) | 将增量新服务插入已有分类树 | 待实现 |
| **向量基线** (`src/vector/`) | ChromaDB 向量检索（对比基线） | ✅ |
| **传统基线** (`src/traditional/`) | MCP 全上下文基线（对比基线） | ✅ |
| **后端** (`src/backend/`) | FastAPI，路由 + 服务编排 | ✅ |
| **前端** (`src/frontend/`) | React + D3.js 可视化 + 管理面板 | ✅ |

**数据流**：
- 注册模块管理服务生命周期 → 输出 service.json → 构建模块、向量基线、传统基线消费
- 构建模块输出分类树（taxonomy.json + class.json）→ 搜索模块使用
- 注册模块检测 service.json 变更 → 通过回调触发向量索引增量同步
- 注册模块跟踪 taxonomy 状态 → 搜索模块查询前检查分类树是否可用

### 2. 对外调用接口

| 模块 | 输入 | 输出 |
|:----:|:----:|:----:|
| **注册** | 配置文件 / HTTP API / CLI | service.json + taxonomy 状态 |
| **构建** | service.json | 分类树（taxonomy.json + class.json） |
| **搜索** | 查询 + 分类树 | 服务列表 + 搜索统计 |
| **增量构建** | 新服务 + 分类树 | 更新后的分类树 |
| **向量基线** | 查询 + service.json | 服务列表 |
| **传统基线** | 查询 + service.json | 服务列表 |

### 3. 逻辑视图

```mermaid
graph TB
    subgraph INPUT[输入源]
        UC([user_config.json])
        AC([api_config.json])
        SK([skills/])
        CLI([CLI])
        API([HTTP API])
    end

    subgraph REG[注册模块]
        RS[RegistryService]
    end

    UC --> RS
    AC --> RS
    SK --> RS
    CLI --> RS
    API --> RS
    RS --> SJ[(service.json)]

    subgraph A2X[A2X 检索]
        BUILD[构建] --> TAX[(分类树)]
        TAX --> SEARCH[搜索]
    end

    subgraph BASELINE[对比基线]
        VEC[向量检索]
        TRAD[传统检索]
    end

    SJ --> BUILD
    SJ --> VEC
    SJ --> TRAD

    QRY([用户查询]) --> SEARCH
    QRY --> VEC
    QRY --> TRAD
    SEARCH --> RES([服务列表])
    VEC --> RES
    TRAD --> RES

    RS -.->|taxonomy 状态| SEARCH
    RS -.->|变更回调| VEC

    NEW([增量新服务]) --> INCR[增量构建]
    TAX <--> INCR

    style TAX fill:#e8eaf6,stroke:#3f51b5,stroke-width:2px
    style SJ fill:#e8eaf6,stroke:#3f51b5,stroke-width:2px
    style RS fill:#fff3e0,stroke:#ff9800
    style BUILD fill:#fff3e0,stroke:#ff9800
    style SEARCH fill:#e8f5e9,stroke:#4caf50
    style VEC fill:#e8f5e9,stroke:#4caf50
    style TRAD fill:#e8f5e9,stroke:#4caf50
    style INCR fill:#e3f2fd,stroke:#2196f3
```

### 4. 顺序图

#### 4a. 远程使用（Web UI / HTTP API → FastAPI 后端）

```mermaid
sequenceDiagram
    participant U as 用户 / Web UI
    participant BE as 后端 (FastAPI)
    participant REG as RegistryService
    participant B as TaxonomyBuilder
    participant T as 分类树
    participant S as A2XSearch
    participant V as 向量索引

    Note over REG,V: 启动阶段
    BE->>REG: startup()
    REG->>REG: 加载配置 + 并行抓取 Agent Card
    REG-->>BE: service.json 就绪
    BE->>V: 向量索引初始同步

    Note over U,T: 构建阶段
    U->>BE: POST /api/datasets/{ds}/build
    BE->>B: TaxonomyBuilder.build() (后台线程)
    B->>T: 生成 taxonomy.json + class.json
    U->>BE: GET /api/datasets/{ds}/build/stream
    BE-->>U: SSE 实时日志

    Note over U,S: 搜索阶段
    U->>BE: POST /api/search
    BE->>REG: check_taxonomy_state()
    REG-->>BE: available
    BE->>S: A2XSearch.search(query)
    S->>T: 读取分类结构
    S-->>BE: 服务列表
    BE-->>U: JSON 响应

    Note over U,REG: 注册变更
    U->>BE: POST /api/datasets/{ds}/services/generic
    BE->>REG: register_generic(req)
    REG->>REG: 更新 _entries → 写 api_config.json → 写 service.json
    REG-->>V: 变更回调 → 增量同步
    REG->>REG: taxonomy → STALE
    BE-->>U: RegisterResponse
    Note right of REG: 需重新 build 后<br/>A2X 搜索才可用
```

#### 4b. 本地使用（CLI → 直接调用 Python 接口）

```mermaid
sequenceDiagram
    participant U as 用户终端
    participant CLI as __main__.py
    participant REG as RegistryService
    participant ST as RegistryStore
    participant FS as 文件系统

    Note over CLI,FS: 每次 CLI 调用都经历：解析参数 → startup → 执行命令

    rect rgb(232, 234, 246)
        Note over CLI,FS: 注册服务
        U->>CLI: python -m a2x_registry.register register-generic ds --name N --desc D
        CLI->>REG: RegistryService(database_dir)
        CLI->>REG: startup()
        REG->>ST: load_user_config + load_api_config + load_skills
        ST->>FS: 读取配置文件
        REG->>REG: 合并 + 并行抓取 Agent Card URL
        CLI->>REG: register_generic(req)
        REG->>REG: 锁内更新 _entries
        REG->>ST: save_api_entry()
        ST->>FS: 写 api_config.json
        REG->>ST: write_service_json()
        ST->>FS: 写 service.json
        REG-->>CLI: RegisterResponse
        CLI-->>U: 输出结果
    end

    rect rgb(200, 230, 201)
        Note over CLI,FS: 构建分类树
        U->>CLI: python -m a2x_registry.a2x.build --service-path database/ds/service.json
        Note right of CLI: 独立进程，直接读取<br/>service.json 构建分类树
        CLI->>FS: 读取 service.json
        CLI->>FS: 写 taxonomy.json + class.json + build_config.json
        CLI-->>U: 构建完成
    end

    rect rgb(255, 243, 224)
        Note over CLI,FS: 查询服务
        U->>CLI: python -m a2x_registry.register list ds --mode admin
        CLI->>REG: RegistryService(database_dir)
        CLI->>REG: startup()
        CLI->>REG: list_entries(ds)
        REG-->>CLI: List[RegistryEntry]
        CLI-->>U: 表格输出
    end

    rect rgb(255, 205, 210)
        Note over CLI,FS: 注销服务
        U->>CLI: python -m a2x_registry.register deregister ds service_id
        CLI->>REG: RegistryService(database_dir)
        CLI->>REG: startup()
        CLI->>REG: deregister(ds, service_id)
        REG->>ST: remove_api_entry()
        ST->>FS: 更新 api_config.json
        REG->>ST: write_service_json()
        ST->>FS: 更新 service.json
        REG-->>CLI: DeregisterResponse
        CLI-->>U: 输出结果
    end
```

### 5. 类图

```mermaid
classDiagram
    class RegistryService {
        +startup() Dict
        +register_generic(req) RegisterResponse
        +register_a2a(req) RegisterResponse
        +register_skill(dataset, zip) SkillResponse
        +deregister(dataset, id) DeregisterResponse
        +list_services(dataset) List~dict~
        +check_taxonomy_state(dataset) TaxonomyState
    }

    class TaxonomyBuilder {
        -config: AutoHierarchicalConfig
        +build(resume: str) void
    }

    class A2XSearch {
        -categories: Dict
        -classes: Dict
        -services: Dict
        +search(query) Tuple~List, SearchStats~
    }

    class VectorSearch {
        -chroma_store: ChromaStore
        +search(query, top_k) List
    }

    class TraditionalSearch {
        -services: List
        +search(query) List
    }

    class LLMClient {
        <<src.common.llm_client>>
        +call(messages, temperature, max_tokens) LLMResponse
        +call_batch(prompts, system_prompt, max_workers) List
    }

    class IncrementalBuilder {
        <<待实现>>
        +add_service(service) List~str~
        +remove_service(service_id) bool
    }

    RegistryService ..> TaxonomyBuilder : service.json → build
    TaxonomyBuilder ..> A2XSearch : taxonomy → search
    RegistryService ..> VectorSearch : 变更回调
    TaxonomyBuilder ..> LLMClient : uses
    A2XSearch ..> LLMClient : uses
    IncrementalBuilder ..> LLMClient : uses
```

### 6. 目录结构

```
src/
├── common/          # 共享工具（models, llm_client, evaluation, naming）
├── a2x/             # A2X 分类树检索（build / search / evaluation / incremental）
├── vector/          # 向量基线（ChromaDB 索引 / search / evaluation）
├── traditional/     # 传统基线（全上下文 search / evaluation）
├── register/        # 服务注册（generic / a2a / skill）
├── backend/         # FastAPI 后端（routers: dataset, build, search, provider）
├── frontend/        # React + Vite + Tailwind + D3.js
└── ui/              # 集成启动器（python -m a2x_registry.ui）
```
