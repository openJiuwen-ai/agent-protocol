# 注册模块设计文档

本文档详细描述注册模块（`src/register/`）的设计。系统整体视图及其他模块设计见 [a2x_design.md](a2x_design.md)。

---

## 1. 流程逻辑说明

注册模块为 A2X Registry 提供服务注册能力，支持三类服务（Generic + A2A Agent + Skill），多数据集，多种注册入口（本地配置文件 / Python API / CLI / Skill 文件夹）。后端 HTTP API（`src/backend/routers/`）通过调用本模块的 Python 接口实现远程访问。

参考 Nacos 设计思路：分离"注册存储"与"消费输出"，但针对单机文件驱动场景做了简化。

### 核心架构

```
user_config.json ──┐
  (用户编辑，只读)   │
                   ├─→ RegistryService ─→ service.json (纯输出)
api_config.json ───┤       │
  (系统写入)        │       │
skills/*/SKILL.md ─┘  RegistryStore
  (文件夹注册)         (文件 I/O)
```

**设计原则**：

- `service.json` 是**纯输出**，从不被注册模块读取；所有状态来自三个输入源
- 三来源合并优先级：`api_config` > `user_config` > `skill_folder`（同 ID 时高优先级覆盖）
- **锁内只操作内存，文件 I/O 在锁外执行**
- 查询方法返回内存副本，不暴露内部引用

### 启动流程

每次使用前必须调用 `startup()`，按三阶段初始化：

1. **Phase 1 — 加载配置文件**：遍历所有数据集目录，读取 `user_config.json`（只读）+ `api_config.json`（缓存到内存）+ `skills/*/SKILL.md`，合并后验证 A2A 条目
2. **Phase 2 — 并行抓取 Agent Card URL**：对包含 `agent_card_url` 的条目，`ThreadPoolExecutor(10)` 并行抓取，成功后更新缓存快照
3. **Phase 3 — 生成输出**：持久化最新的 api_config 快照，生成 service.json，计算 taxonomy 状态

### 注册流程

```
register_generic / register_a2a:
  → 查数据集 register_config.json 确认类型在允许列表内
  → 统一格式校验（由对应 FormatValidator 从老→新依次尝试，首个通过即接受）
  → 创建 RegistryEntry
  → 锁内更新 _entries（status: registered / updated）
  → 锁外持久化写 api_config.json（如 persistent=True）
  → _regenerate_output → 对比内存缓存，变化时写 service.json + 触发回调

register_skill(dataset, zip_bytes):
  → 查数据集 register_config.json 确认 "skill" 在允许列表内
  → 解压 ZIP → 校验 SKILL.md 含 name + description
  → 统一格式校验（SkillValidator）
  → 存储至 skills/{name}/（已存在则覆盖）
  → 锁内更新 _entries → _regenerate_output
```

### 注销流程

```
deregister(dataset, service_id):
  → 锁内检查 source + 删除 _entries
  → user_config 来源: 拒绝（ValueError）
  → skill_folder 来源: 拒绝（需使用 deregister_skill）
  → api_config: 锁外从文件删除
  → ephemeral: 仅内存删除
  → _regenerate_output

deregister_skill(dataset, name):
  → 锁内删除 _entries
  → 将 skills/{name}/ 移至 removed_skills/{name}/（同名覆盖，非直接删除）
  → _regenerate_output
```

### 更新流程

```
update_service(dataset, service_id, updates):
  → 锁内定位 entry；user_config 来源 → 拒绝
  → 按类型 dispatch：
    generic: 校验字段名 ⊆ {name, description, inputSchema, url}
             → model_dump 合并 updates → 重新构造 GenericServiceData
    a2a:     requires entry.agent_card 已解析
             → model_dump 合并 updates → 重新构造 AgentCard（extra=allow 保留自定义字段）
    skill:   校验字段名 ⊆ {name, description, license}
             → 若 name 变化：rename skills/{old}/ → skills/{new}/
             → 重写 SKILL.md frontmatter（只写变化过的字段，保留 body 与其它键）
  → 锁内替换 _entries[service_id]
  → api_config 来源：save_api_entry 持久化
  → _regenerate_output
  → 若 {name, description} ∩ changed_fields 非空：_mark_taxonomy_stale
```

**更新契约**：

- 顶层字段 upsert：相同键直接替换，新键追加；不删除既有字段。
- **不触发格式校验** —— 既然只增不减必要字段，原始校验的不变式仍成立。
- 只有 `name` 或 `description` 发生实际变化时，才把 taxonomy 标记为 STALE；
  仅改 `url` / `inputSchema` 等不影响分类 hash 的字段不会触发重建提示。
- 返回 `UpdateResponse{service_id, dataset, changed_fields[], taxonomy_affected}`。

### 变更检测

`_regenerate_output` 每次重新生成 output 列表后与 `_output_cache` 中上次结果做内存对比，仅实际变化时写盘并触发回调。无独立 hash 文件。

### Taxonomy 状态联动

每次 CRUD 操作后将 taxonomy 状态标记为 `STALE`（仅当前状态为 `AVAILABLE` 时）。下次搜索前 `check_taxonomy_state()` 重新对比 service hash 与 `build_config.json` 中的 hash，决定是否可用。

## 2. 对外调用接口

### CLI 入口

```bash
python -m a2x_registry.register [--database-dir DIR] [--config FILE] [--json] [-v] <command>
```

| 子命令 | 说明 | 示例 |
|--------|------|------|
| `status [--dataset DS]` | 查看注册状态 | `python -m a2x_registry.register status` |
| `datasets` | 列出所有数据集 | `python -m a2x_registry.register datasets` |
| `list DATASET [--mode admin\|browse]` | 列出服务 | `python -m a2x_registry.register list publicMCP` |
| `get DATASET SERVICE_ID` | 查看单个服务 | `python -m a2x_registry.register get default agent_xxx` |
| `register-generic DATASET --name N --desc D` | 注册 Generic | `python -m a2x_registry.register register-generic default --name "API" --desc "..."` |
| `register-a2a DATASET (--url U \| --card-file F)` | 注册 A2A | `python -m a2x_registry.register register-a2a default --url https://...` |
| `register-skill DATASET ZIP` | 上传 Skill | `python -m a2x_registry.register register-skill default skill.zip` |
| `update DATASET SERVICE_ID [--json F \| --set k=v ...] [--name N] [--desc D]` | 部分字段更新 | `python -m a2x_registry.register update default generic_xxx --desc "new"` |
| `deregister DATASET SERVICE_ID` | 注销服务 | `python -m a2x_registry.register deregister default generic_xxx` |
| `deregister-skill DATASET NAME` | 删除 Skill | `python -m a2x_registry.register deregister-skill default my-skill` |
| `create-dataset NAME [--embedding-model M] [--formats SPEC]` | 创建数据集（可声明允许格式） | `python -m a2x_registry.register create-dataset myDS --formats generic,a2a:v1.0` |
| `delete-dataset NAME [--confirm]` | 删除数据集 | `python -m a2x_registry.register delete-dataset old --confirm` |
| `get-register-config DATASET` | 查看允许的注册格式 | `python -m a2x_registry.register get-register-config default` |
| `set-register-config DATASET --formats SPEC` | 修改允许的注册格式 | `python -m a2x_registry.register set-register-config default --formats a2a:v1.0` |

全局选项：`--json` 输出机器可读 JSON；`-v` 启用 DEBUG 日志。

向后兼容旧用法：`python -m a2x_registry.register --status` / `python -m a2x_registry.register --config path`。

### Python 接口

```python
from a2x_registry.register import RegistryService
from a2x_registry.register.models import RegisterGenericRequest, RegisterA2ARequest

svc = RegistryService(database_dir=Path("database"))
svc.startup()

# 注册
svc.register_generic(RegisterGenericRequest(dataset="ds", name="...", description="..."))
svc.register_a2a(RegisterA2ARequest(dataset="ds", agent_card_url="https://..."))
svc.register_skill("ds", zip_bytes)
svc.register_batch(entries, dataset="ds")

# 更新（部分字段 upsert；不触发格式校验）
svc.update_service("ds", "service_id", {"description": "new", "url": "..."})

# 注销
svc.deregister("ds", "service_id")
svc.deregister_skill("ds", "skill_name")
svc.get_skill_zip("ds", "name")    # → bytes (ZIP)

# 查询
svc.list_services("ds")         # → List[dict]  (service.json 格式)
svc.list_entries("ds")          # → List[RegistryEntry]  (含 source 信息)
svc.get_entry("ds", "id")       # → Optional[RegistryEntry]
svc.get_status("ds")            # → RegistryStatus
svc.list_datasets()             # → List[str]
svc.check_taxonomy_state("ds")  # → Optional[TaxonomyState]

# 数据集生命周期
svc.create_dataset("name", embedding_model="all-MiniLM-L6-v2",
                   formats={"generic": "v0.0", "a2a": "v1.0"})
svc.delete_dataset("name")

# 注册格式配置
svc.get_register_config("name")                    # → {"generic": "v0.0", ...}
svc.set_register_config("name", {"a2a": "v1.0"})   # 覆盖式更新

# 回调（后端用于触发向量索引同步）
svc.set_on_service_changed(lambda dataset: ...)
```

### 输入输出格式

**输入** — `user_config.json` / `api_config.json`：
```json
{
  "services": [
    {"type": "generic", "name": "Calculator", "description": "Basic arithmetic", "url": "..."},
    {"type": "a2a", "agent_card_url": "https://.../.well-known/agent.json"},
    {"type": "a2a", "agent_card": {"name": "...", "description": "...", "skills": [...]}}
  ]
}
```

**输出** — `service.json`（统一格式 `{id, type, name, description, metadata}`）：
```json
[
  {
    "id": "generic_abc123...", "type": "generic",
    "name": "Calculator", "description": "Basic arithmetic",
    "metadata": {"url": "...", "inputSchema": {...}}
  },
  {
    "id": "agent_def456...", "type": "a2a",
    "name": "Weather Agent", "description": "Provides weather info. Skills: [get_forecast] ...",
    "metadata": {"name": "...", "description": "...", "url": "...", "skills": [...]}
  },
  {
    "id": "skill_789abc...", "type": "skill",
    "name": "algorithmic-art", "description": "Creating algorithmic art...",
    "metadata": {"skill_path": "skills/algorithmic-art", "license": "...", "files": ["SKILL.md", ...]}
  }
]
```

- Generic: `metadata` 含 `url`（可选）和 `inputSchema`（可选）
- A2A: `metadata` 为完整 AgentCard（含非标准字段，`extra="allow"`）；`description` 由 `build_description()` 聚合主描述 + skills
- Skill: `metadata` 含 `skill_path`、`license`、`files`
- 未解析的 A2A（URL fetch 失败且无缓存）：`description` 为 `"Unresolved agent card: <url>"`

### 文件布局

```
database/{dataset}/
    user_config.json      ← 用户手动编辑（系统只读）
    api_config.json       ← HTTP/CLI 注册的持久化服务（系统写入）
    register_config.json  ← 该数据集允许的注册格式列表（类型 + min_version）
    service.json          ← 纯输出（从不作为输入读取）
    skills/               ← Skill 文件夹（每个子目录含 SKILL.md）
    removed_skills/       ← 已注销的 Skill（移动而非删除）
    taxonomy/
        taxonomy.json     ← 分类树结构
        class.json        ← 分类元数据
        build_config.json ← 构建参数 + service_hash
```

| 文件 | 谁写 | 谁读 | 用途 |
|------|------|------|------|
| `user_config.json` | 用户 | 系统（启动时） | 用户声明的服务 |
| `api_config.json` | 系统 | 系统（启动时 + 运行时） | HTTP/CLI 注册的持久化服务 |
| `register_config.json` | 系统 / 用户 | 系统（启动时 + 每次注册） | 数据集允许的注册格式（类型 → min_version） |
| `skills/*/SKILL.md` | 用户/系统 | 系统（启动时 + 运行时） | Skill 文件夹注册源 |
| `removed_skills/` | 系统 | — | 已注销 Skill 的归档 |
| `service.json` | 系统 | A2X / 向量搜索 | 合并后的输出 |
| `build_config.json` | 构建模块 | 注册模块 | 含 `service_hash` 用于 taxonomy 状态判断 |

## 3. 逻辑视图

```mermaid
flowchart TB
    UC([user_config.json]) --> LOAD
    AC([api_config.json]) --> LOAD
    SK([skills/]) --> LOAD

    subgraph STARTUP[启动 — startup]
        direction TB
        LOAD[Phase 1: 加载配置<br/>合并 + A2A 校验] --> FETCH
        FETCH[Phase 2: 并行抓取<br/>agent_card_url] --> GEN
        GEN[Phase 3: 生成输出<br/>+ 计算 taxonomy 状态]
    end

    GEN --> MEM[(内存状态<br/>_entries<br/>_output_cache<br/>_taxonomy_states)]
    GEN --> SJ([service.json])

    subgraph CRUD[运行时 CRUD]
        direction TB
        REG[register_generic<br/>register_a2a<br/>register_skill] --> UPD
        DEREG[deregister<br/>deregister_skill] --> UPD
        UPD[锁内更新 _entries] --> PERSIST{persistent?}
        PERSIST -->|是| WRITE_API[写 api_config.json]
        PERSIST -->|否| REGEN
        WRITE_API --> REGEN
        REGEN[_regenerate_output<br/>对比缓存 → 写 service.json]
        REGEN --> STALE[标记 taxonomy STALE]
    end

    MEM <--> CRUD
    REGEN --> SJ
    REGEN -->|变更回调| CB([on_service_changed<br/>→ 向量索引同步])

    subgraph QUERY[查询（只读）]
        direction LR
        LS[list_services] --> MEM_R[(返回内存副本)]
        LE[list_entries] --> MEM_R
        GE[get_entry] --> MEM_R
        GS[get_status] --> MEM_R
        CTS[check_taxonomy_state] --> MEM_R
    end

    MEM --> QUERY

    style UC fill:#e3f2fd,stroke:#1565c0
    style AC fill:#e3f2fd,stroke:#1565c0
    style SK fill:#e3f2fd,stroke:#1565c0
    style SJ fill:#e8eaf6,stroke:#3f51b5,stroke-width:2px
    style MEM fill:#fff3e0,stroke:#ff9800
    style CB fill:#fff9c4,stroke:#f9a825
    style MEM_R fill:#c8e6c9,stroke:#4caf50
```

## 4. 顺序图

### 启动 + 注册 + 注销

```mermaid
sequenceDiagram
    participant U as 调用方
    participant RS as RegistryService
    participant ST as RegistryStore
    participant NET as Network
    participant FS as 文件系统

    rect rgb(227, 242, 253)
        Note over RS,FS: Phase 1 — 加载配置
        U->>RS: startup()
        RS->>RS: _discover_datasets()
        loop 每个 dataset
            RS->>ST: load_user_config()
            ST->>FS: read user_config.json
            RS->>ST: load_api_config()
            ST->>FS: read api_config.json
            RS->>ST: load_skills()
            ST->>FS: scan skills/*/SKILL.md
            RS->>RS: 合并（api > user > skill）+ A2A 校验
        end
    end

    rect rgb(255, 243, 224)
        Note over RS,NET: Phase 2 — 并行抓取 Agent Card
        par 最多 10 并行
            RS->>NET: fetch agent_card_url
            NET-->>RS: AgentCard JSON
        end
        RS->>RS: 锁内更新 entry.agent_card
    end

    rect rgb(232, 234, 246)
        Note over RS,FS: Phase 3 — 生成输出
        loop 每个 dataset
            RS->>ST: save_api_batch (更新缓存快照)
            RS->>RS: _regenerate_output
            RS->>ST: write_service_json
            RS->>RS: _init_taxonomy_state
        end
    end
    RS-->>U: {dataset: TaxonomyState}

    rect rgb(200, 230, 201)
        Note over U,FS: 注册 Generic 服务
        U->>RS: register_generic(req)
        RS->>RS: 校验 name/description
        RS->>RS: 锁内更新 _entries
        RS->>ST: save_api_entry (写 api_config.json)
        RS->>RS: _regenerate_output
        RS->>ST: write_service_json (仅变化时)
        RS-->>U: RegisterResponse
    end

    rect rgb(255, 205, 210)
        Note over U,FS: 注销服务
        U->>RS: deregister(dataset, service_id)
        RS->>RS: 锁内检查 source + 删除
        alt source = api_config
            RS->>ST: remove_api_entry
        end
        RS->>RS: _regenerate_output
        RS-->>U: DeregisterResponse
    end
```

## 5. 类图

```mermaid
classDiagram
    class RegistryService {
        -_database_dir: Path
        -_global_config_path: Path
        -_stores: Dict~str, RegistryStore~
        -_entries: Dict~str, Dict~str, RegistryEntry~~
        -_output_cache: Dict~str, List~dict~~
        -_taxonomy_states: Dict~str, TaxonomyState~
        -_lock: threading.Lock
        -_on_service_changed: Callable
        +startup() Dict~str, TaxonomyState~
        +register_generic(req) RegisterResponse
        +register_a2a(req) RegisterResponse
        +register_skill(dataset, zip_bytes) SkillResponse
        +register_batch(entries, dataset, persistent) void
        +update_service(dataset, service_id, updates) UpdateResponse
        +deregister(dataset, service_id) DeregisterResponse
        +deregister_skill(dataset, name) SkillResponse
        +list_services(dataset) List~dict~
        +list_entries(dataset) List~RegistryEntry~
        +get_entry(dataset, service_id) RegistryEntry
        +get_status(dataset) RegistryStatus
        +list_datasets() List~str~
        +check_taxonomy_state(dataset) TaxonomyState
        +create_dataset(name, embedding_model) Path
        +delete_dataset(name) void
        +set_on_service_changed(callback) void
        -_regenerate_output(dataset) bool
        -_mark_taxonomy_stale(dataset) void
    }

    class RegistryStore {
        -_dir: Path
        -_api_entries: Dict~str, RegistryEntry~
        -_lock: threading.Lock
        +load_user_config() List~RegistryEntry~
        +load_api_config() List~RegistryEntry~
        +load_skills() List~RegistryEntry~
        +save_api_entry(entry) void
        +remove_api_entry(service_id) bool
        +save_api_batch(entries) void
        +save_skill_zip(zip_bytes) SkillData
        +remove_skill(name) bool
        +rename_skill(old_name, new_name) void
        +update_skill_md(name, updates) void
        +get_skill_zip(name) bytes
        +write_service_json(services) void
    }

    class RegistryEntry {
        +service_id: str
        +type: generic | a2a | skill
        +source: user_config | api_config | ephemeral | skill_folder
        +service_data: GenericServiceData
        +agent_card: AgentCard
        +agent_card_url: str
        +skill_data: SkillData
    }

    class AgentCard {
        +name: str
        +description: str
        +version: str
        +url: str
        +skills: List~AgentSkill~
        +provider: AgentProvider
        +capabilities: AgentCapabilities
        «extra=allow»
    }

    class ValidationResult {
        +valid: bool
        +service_type: str
        +matched_version: str
        +errors: List~str~
        +warnings: List~str~
    }

    class FormatValidator {
        +service_type: str
        +SUPPORTED_VERSIONS: List~str~
        +validate(payload, min_version) ValidationResult
        #_check_version(payload, version) (errors, warnings)
    }

    class GenericValidator
    class A2AValidator
    class SkillValidator

    FormatValidator <|-- GenericValidator
    FormatValidator <|-- A2AValidator
    FormatValidator <|-- SkillValidator
    FormatValidator --> ValidationResult : returns

    RegistryService --> RegistryStore : owns (per dataset)
    RegistryService --> RegistryEntry : manages
    RegistryService --> FormatValidator : dispatches via validate_service
    RegistryStore --> RegistryEntry : reads/writes
    RegistryEntry --> AgentCard : optional
    RegistryEntry --> GenericServiceData : optional
    RegistryEntry --> SkillData : optional
```

## 6. 三类服务

### Generic 服务

```json
{"type": "generic", "name": "Calculator", "description": "Basic arithmetic", "inputSchema": {...}, "url": "..."}
```

- `url` 和 `inputSchema` 均可选
- `service_id` 自动生成：`generic_{sha256(name)[:16]}`

### A2A Agent

**完整 AgentCard**：
```json
{"type": "a2a", "agent_card": {"name": "...", "description": "...", "skills": [...]}}
```

**URL 引用**（系统抓取并缓存快照到 api_config.json）：
```json
{"type": "a2a", "agent_card_url": "https://.../.well-known/agent.json"}
```

- `service_id` 自动生成：`agent_{sha256(name)[:16]}`
- AgentCard 模型使用 `extra="allow"`，非标准字段完整保留
- 格式验证支持 v0.0（最宽松）和 v1.0（完整 A2A 规范），先严后宽匹配

### Skill

以文件夹存储在 `skills/{name}/`，每个文件夹必须包含 `SKILL.md`：

```yaml
---
name: algorithmic-art
description: Creating algorithmic art using p5.js...
license: Complete terms in LICENSE.txt
---
```

- 注册：ZIP 上传或直接放入 `skills/` 目录
- 注销：文件夹移至 `removed_skills/`（而非删除）
- `service_id` 自动生成：`skill_{sha256(name)[:16]}`

## 7. 注册格式校验

### 7.1 统一模型

每种服务类型（`generic` / `a2a` / `skill`）拥有独立的 `FormatValidator` 子类，
声明其 `SUPPORTED_VERSIONS`（从老到新排序）。数据集通过 `register_config.json`
声明所接受的类型及每种类型的**最老协议版本**（`min_version`）。

**执行规则**：

1. 注册请求的 `type` 必须在数据集的允许列表内，否则直接拒绝。
2. 从 `min_version` 起，按 `SUPPORTED_VERSIONS` **从老到新**依次尝试。
3. 任何版本通过 → 整体通过，记录首个匹配到的版本为 `matched_version`。
4. 全部失败 → 抛出 `ValueError`，错误信息包含最新版本的具体缺失字段。
5. 格式校验**只检验必填字段是否存在**；不做业务语义检查。

```
FormatValidator (ABC)
├── GenericValidator        SUPPORTED_VERSIONS = ["v0.0"]
├── SkillValidator          SUPPORTED_VERSIONS = ["v0.0"]
└── A2AValidator            SUPPORTED_VERSIONS = ["v0.0", "v1.0"]
```

### 7.2 版本规则

| 类型 | 版本 | 必填字段 | 用途 |
|------|------|---------|------|
| **generic** | v0.0 | name, description | 通用服务基线 |
| **skill** | v0.0 | name, description | SKILL.md frontmatter 基线 |
| **a2a** | v0.0 | name, description | 兼容非标准 Agent Card |
| **a2a** | v1.0 | name, description, version, url, capabilities, defaultInputModes, defaultOutputModes, skills (含 id/name/description/tags) | 完整 A2A v1.0 规范 |

**所有 v0.0 只校验 `name` 和 `description`**（由基类 `_check_name_description` 共享实现）。

### 7.3 数据集级配置

每个数据集目录下的 `register_config.json`：

```json
{
  "formats": {
    "generic": "v0.0",
    "a2a": "v0.0",
    "skill": "v0.0"
  }
}
```

- 默认（文件缺失时）：三种类型全部允许，`min_version` 均为 `v0.0`。
- 未声明的类型：拒绝该类型的所有注册。
- 声明类型但 `min_version=v1.0`：只接受 v1.0 及以上版本的 payload。

### 7.4 兼容旧接口

`validate_agent_card(card, allowed_versions)` 保留作为薄封装：
将 `allowed_versions` 集合中的最老版本映射为 `min_version` 后调用
`validate_service("a2a", …)`。新代码请直接使用 `validate_service(type, payload, min_version)`。

## 8. Taxonomy 状态 (TaxonomyState)

| 状态 | 含义 |
|------|------|
| `available` | service hash 与 `build_config.json` 一致，A2X 搜索可用 |
| `unavailable` | hash 不一致，A2X 搜索被阻断 |
| `stale` | CRUD 发生后尚未重新检查，下次查询时重新评估 |
| `nonexistent` | 尚未构建分类树（或 `build_config.json` 不存在） |
| `None`（返回值） | 该数据集不受注册模块管理，不做限制 |

状态由 `check_taxonomy_state()` 计算：读取 `build_config.json` 中的 `service_hash`，与当前 service 列表的 hash（name+description 对的 SHA256）对比。每次 CRUD 后置为 `STALE`（仅当前为 `AVAILABLE` 时），下次搜索前重新评估。

## 9. 线程安全

```
RegistryService._lock
  └── 保护: _entries, _output_cache, _taxonomy_states

RegistryStore._lock
  └── 保护: _api_entries + api_config.json 写入
```

- `_do_register`：锁内更新 `_entries` → 锁外写 `api_config.json` → 锁外 `_regenerate_output`
- `deregister`：锁内删除 + 记录 source → 锁外删除 `api_config` 条目 → 锁外 `_regenerate_output`
- `_regenerate_output`：锁内生成 output + 对比缓存 → 锁外写文件 + 触发回调
- `_fetch_agent_cards_parallel`：并行 fetch 后在锁内更新 `entry.agent_card`
- 查询方法（`list_services` 等）返回副本，不暴露内部引用

## 10. 向量数据库同步

注册模块与向量搜索引擎通过**回调**解耦：

```python
registry_svc.set_on_service_changed(callback)
# callback: (dataset: str) -> None
```

每次 `_regenerate_output` 检测到 service.json 实际变更后，触发回调。后端注册该回调为 `search_service.schedule_vector_sync(dataset)`，在独立线程中做增量同步（upsert 新增/变化条目，删除已注销条目）。

## 11. 模块结构

```
src/register/
    __init__.py          导出 RegistryService, RegistryStore, validate_service, FormatValidator, ...
    models.py            Pydantic 数据模型（RegistryEntry, AgentCard, Request/Response 等）
    store.py             RegistryStore: 单数据集文件 I/O（含 register_config 读写）
    service.py           RegistryService: 多数据集业务编排（单锁保护所有状态）
    agent_card.py        Agent Card URL 抓取 + description 聚合
    validation.py        统一格式校验（FormatValidator 基类 + 三种子类；validate_service 入口）
    __main__.py          CLI 入口（13 个子命令 + 向后兼容旧用法）
```

## 12. 效率

| 场景 | 文件写入 | 说明 |
|------|---------|------|
| 启动 | 每数据集 1~2 次 | service.json 必写（_output_cache 初始为空）+ api_config 快照（如有 api_config 条目） |
| 单次持久注册 | 2 次 | api_config + service.json |
| 单次临时注册 | 0~1 次 | 仅 service.json（如内容变化） |
| 批量 N 个 | 2 次 | 不是 N 倍（单次 api_config + 单次 service.json） |
| 查询 | 0 次 | 直接返回内存缓存副本 |
