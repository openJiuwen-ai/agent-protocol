# A2X 鉴权模块设计

> 适用 v0.1.7+。本文档面向运维 / 开发者。**基本用法**见 [README.md](../README.md) 与 [client/README.md](../client/README.md) 的相关章节；本文聚焦设计原理、文件布局、模块依赖与安全不变式。

## 1. 定位与设计原则

注册中心默认完全开放：任何匿名调用方都可以注册 / 改 / 删任何服务，预约也接受任意 `holder_id`。鉴权模块在此基础上增加 **静态 API Key + 三档角色 + 按 namespace 作用域** 的最小可用基线，**且保证完全向前兼容**：

| 层级 | 默认 | 启用方式 | 行为 |
|------|------|---------|------|
| 注册中心 | 关闭 | `a2x-registry auth init` | 启用前 `/api/auth/*` 端点返回 404；所有 namespace 行为与启用前完全一致 |
| Namespace | `auth_required=false` | 创建时 `POST /api/datasets {auth_required: true}`，或后续 `POST /{ds}/auth-config` | 默认值的 namespace 永远不需要 token；只有显式声明的 namespace 进入鉴权路径 |

三档角色：

| 角色 | 数据权限 | 管理权限 | namespace 作用域 |
|------|---------|---------|------------------|
| **admin** | 所有 namespace 的服务读/写/删；所有预约的查看与释放 | 创建 / 删除 dataset；管理 principal / key；切 LLM provider；改 dataset-level 配置 | 全局（`namespaces=None`） |
| **provider** | 注册服务（`owner_id` 写为自己）；改/删自己 owned 的服务 | 自管自己的 key | 绑定显式 namespace 列表 |
| **user** | 只读：list / get；reserve / release 自己持有的 lease | 自管自己的 key | 绑定显式 namespace 列表 |

设计约束：

- **高内聚**：所有鉴权代码集中在 [a2x_registry/auth/](../a2x_registry/auth/)；运行时数据在 [a2x_registry/auth/auth_data/](../a2x_registry/auth/) 子目录（git 忽略，不进 wheel）
- **低耦合**：`register/` 子树**不 import** `auth/`；二者通过定义在 [a2x_registry/common/auth_context.py](../a2x_registry/common/auth_context.py) 的中立 dataclass `AuthContext` 传递调用方身份
- **FastAPI 局部化**：HTTP / Header / Depends 仅出现在 [auth/deps.py](../a2x_registry/auth/deps.py) 与 [auth/router.py](../a2x_registry/auth/router.py)；`AuthStore` 与 token / hash 工具是纯 Python，可以独立单测

---

## 2. 数据模型

### 2.1 Principal

代表一个被鉴权身份。一个 Principal 可挂多把 API key（轮换、按设备分发、独立撤销）。

```
Principal
├─ id          : str               # "u_<12 hex>"，稳定不可变
├─ handle      : str               # 人类可读名，唯一，可改
├─ role        : "admin" | "provider" | "user"
├─ namespaces  : list[str] | None  # admin：None（全部）；其他：显式列表，空 [] 也合法
├─ created_at  : ISO8601
├─ disabled_at : ISO8601 | null    # 非空即冻结，所有 key 失效
└─ note        : str
```

不变式：

- `role == "admin"` ↔ `namespaces is None`，由 `AuthStore.create_principal` / `update_principal` 强制
- `namespaces == []` 是合法状态（"暂停访问"），admin 可以暂存一个未授权的 principal
- 删除 dataset 不会自动从 principal 的 namespaces 列表清掉它（dangling 引用安全 fail 为 403，admin 可后续 PATCH 修正）

### 2.2 ApiKey

```
ApiKey
├─ key_id        : str    # "k_<12 hex>"
├─ principal_id  : str
├─ key_hash      : str    # sha256(plaintext token) hex
├─ key_prefix    : str    # plaintext token 的前 12 字符（"a2x_pat_xxxx"），用于审计与 UI 展示
├─ name          : str
├─ created_at    : ISO8601
├─ expires_at    : ISO8601 | null   # 占位，Phase 1 不强制
├─ last_used_at  : ISO8601 | null
└─ revoked_at    : ISO8601 | null
```

- 服务端**只存 sha256 哈希**。plaintext 仅在 ① bootstrap stderr banner、② `POST /api/auth/principals` 响应 body、③ `POST /api/auth/keys` 响应 body 这三处出现，写盘前已被替换为 hash
- 撤销后 `key_hash` 从内存的 `_keys_by_hash` 索引被立即移除（fast-fail）

### 2.3 AuthContext

服务层（`RegistryService.register_a2a` 等）接受的中立调用方上下文：

```python
@dataclass(frozen=True)
class AuthContext:
    principal_id: str
    role: str                         # "admin" | "provider" | "user"
    namespaces: frozenset[str] | None
```

`None` 即调用方未鉴权（匿名 namespace 或注册中心未 bootstrap）。`Principal.to_context()` 是 auth 模块与 register 模块的唯一握手点。

### 2.4 RegistryEntry 的 owner_id 字段

[register/models.py:RegistryEntry](../a2x_registry/register/models.py) 新增 `owner_id: Optional[str] = None`。语义：

| 取值 | 含义 |
|------|------|
| `None` | 系统服务 / 来自 `user_config.json` / 匿名 namespace 上注册的 / 鉴权启用前注册的 |
| `<principal_id>` | 在 auth-required namespace 上由 provider/admin 注册时由服务端写入 |

序列化时 **owner_id is None 的 entry 不写入 `owner_id` 字段** —— 让匿名 namespace 的 `api_config.json` 与鉴权启用前的输出 byte-equal（向前兼容 lockfile 测试依赖此）。

---

## 3. 文件布局

```
a2x_registry/auth/                # 代码
├── __init__.py                   # 公开 re-export
├── models.py                     # Pydantic: Principal / ApiKey
├── tokens.py                     # 生成 / 哈希 / 前缀
├── store.py                      # AuthStore: 文件 IO + 内存索引 + 鉴权 hot path
├── deps.py                       # FastAPI Depends: authorize / require_admin / ...
├── router.py                     # /api/auth/* 端点
├── cli.py                        # `a2x-registry auth init / reset-admin`
├── errors.py                     # AuthenticationError / AuthorizationError
└── auth_data/                    # 运行时数据 —— .gitignore 排除，不进 wheel
    ├── principals.json
    ├── api_keys.json
    └── audit.log

a2x_registry/common/
└── auth_context.py               # 中立 AuthContext dataclass（register/ 与 auth/ 共用）
```

per-dataset 配置：

```
database/<namespace>/
├── service.json                  # 已有
├── api_config.json               # 已有；entry 现在可能带 owner_id
├── register_config.json          # 已有
├── vector_config.json            # 已有
├── auth_config.json              # 新增
│                                 # {"required": false, "schema_version": 1}
│                                 # 缺失 → required: false（向前兼容默认）
└── ...
```

**自定义路径**：`$A2X_REGISTRY_AUTH_DATA` 环境变量可覆盖 `auth_data/` 位置（部署到只读包目录时把数据落到可写卷）。

---

## 4. 启用流程

```
1. (一次性)  a2x-registry auth init
                 → 写 principals.json + api_keys.json
                 → stderr 打印 root admin token 一次
                 → operator 保存到密码管理器

2. (admin)   POST /api/auth/principals (用 root token)
                 body: {handle, role: "provider", namespaces: [...]}
                 → 响应 body 含 plaintext token，operator 带外发给 provider

3. (provider) 客户端持 token 调 /api/datasets/<ns>/services/a2a
                 → 服务端 ctx.principal_id 写入 entry.owner_id
```

各步骤都是显式的；不存在 "服务器启动自动印一个 token" 的隐式 bootstrap。这是为了让"从未 `auth init` 的注册中心"在行为上与鉴权代码不存在时完全一致。

---

## 5. 鉴权 hot path

唯一对外的 FastAPI 依赖：[`authorize`](../a2x_registry/auth/deps.py)。三条决策分支：

```
                          ┌───────────────────────────────────┐
  请求到达               │ Depends(authorize)                │
       ─────────────────►│                                   │
                          │ 1. 注册中心未 bootstrap (store None)  ─→ 返回 None  (anon)
                          │ 2. 路径含 {dataset} 且 auth_required=false ─→ 返回 None  (anon)
                          │ 3. 否则严格路径：                      │
                          │     a. 解析 Bearer token；缺/格式错 → 401 │
                          │     b. authenticate(token) → AuthContext  │
                          │     c. namespace 在 ctx.namespaces 里？  │
                          │        是 / 或 admin → 返回 ctx            │
                          │        否 → 403 + permission.denied 审计 │
                          └───────────────────────────────────┘
```

服务层在 `caller` 非空时再做一层 fine-grained 检查：

| 操作 | service 层校验 |
|------|----------------|
| `register_*` | `_assert_can_register(caller)`：拒绝 user 角色（admin/provider 通过） |
| `update_service / deregister / replace` | `_assert_owner(entry, caller)`：admin 短路；否则 owner_id 必须等于 caller.principal_id；owner_id is None 视作"无主"，只有 admin 能动 |
| `reserve_services` | 强制 `holder_id = caller.principal_id`，忽略 body 里的值 |
| `release_reservation / extend` | holder_id 必须等于 caller.principal_id（admin 例外） |

> 用 PUT body 试图夹带 `owner_id` 改归属：在路由层就被 [_FORBIDDEN_UPDATE_FIELDS](../a2x_registry/backend/routers/dataset.py) 过滤掉。

---

## 6. 端点鉴权矩阵

`P` = 完全公开；`U` = 任意已认证；`O` = entry.owner_id 等于 caller 或 admin；`A` = 仅 admin；`A?` = 鉴权未初始化时退化为公开。

### 6.1 读路径

| 方法 | 路径 | 匿名 ns | auth-required ns |
|------|------|--------|------------------|
| GET | `/api/datasets` | P | P |
| GET | `/api/datasets/{ds}/services` | P | U + namespace check |
| GET | `/api/datasets/{ds}/services/{sid}` | P | U + namespace check |
| GET | `/api/datasets/{ds}/taxonomy` | P | U + namespace check |
| GET | `/api/datasets/{ds}/default-queries` | P | U + namespace check |
| GET | `/api/datasets/{ds}/register-config` | P | U + namespace check |
| GET | `/api/datasets/{ds}/vector-config` | P | U + namespace check |
| GET | `/api/datasets/{ds}/auth-config` | P | P（开放，便于 SDK / UI 探测） |
| GET | `/api/datasets/{ds}/skills/{name}/download` | P | U + namespace check |
| GET | `/api/datasets/{ds}/build/status` | P | P |
| POST | `/api/search` | P | P（WS 同；Phase 1 不分 namespace 鉴权，文档已标） |

### 6.2 写路径（per-entry，owner-scoped）

| 方法 | 路径 | 匿名 ns | auth-required ns |
|------|------|--------|------------------|
| POST | `/services/generic` `/services/a2a` | P | provider 或 admin + namespace check + 写入 owner_id |
| PUT | `/services/{sid}` | P | O + 过滤 owner_id 字段 |
| DELETE | `/services/{sid}` | P | O |
| POST | `/skills` | P | provider/admin + namespace check |
| DELETE | `/skills/{name}` | P | O |

### 6.3 预约（holder = caller）

| 方法 | 路径 | 匿名 ns | auth-required ns |
|------|------|--------|------------------|
| POST | `/reservations` | P（接受 body holder_id） | U + namespace check；**body holder_id 被忽略**，强制 caller.principal_id |
| DELETE | `/reservations/{holder_id}` | P | U + namespace check + holder_id == caller.principal_id |
| DELETE | `/reservations/{holder_id}/{sid}` | P | 同上 |
| POST | `/reservations/{holder_id}/extend` | P | 同上 |
| DELETE | `/services/{sid}/lease` | P | O |

### 6.4 Dataset-level 与全局管理

| 方法 | 路径 | 鉴权未初始化 | 已初始化 |
|------|------|-------------|---------|
| POST | `/api/datasets` (`auth_required=false`) | P | P |
| POST | `/api/datasets` (`auth_required=true`) | 409 `auth_not_initialized` | A |
| DELETE | `/api/datasets/{ds}` | P | A?（admin if any auth ns；anon ns 仍 P） |
| POST | `/{ds}/register-config` / `vector-config` | P | A?  |
| POST | `/{ds}/auth-config` | 404 | A（require_admin_strict，独立于 namespace 状态） |
| POST | `/api/providers/{name}` | P | A?  |
| POST | `/{ds}/build` / DELETE 同 | P | A?  |
| `/api/auth/*` 全部 | 404 `auth_not_initialized` | U / O / A（按子路由） |

`A?` 的含义：鉴权未初始化时这些端点保持完全匿名（向前兼容旧部署）；一旦 `auth init` 跑过，dataset-level 操作就全部需要 admin。

---

## 7. 安全不变式

1. **Token plaintext 仅出现三个出口**：bootstrap CLI stderr、`POST /api/auth/principals` 响应、`POST /api/auth/keys` 响应。任何持久化（`api_keys.json`）、日志（`audit.log`）、调试输出都只能看到 `key_hash` 或 `key_prefix`。回归测试用 regex `a2x_pat_[A-Za-z0-9_-]{20,}` 扫 audit.log。
2. **`owner_id` 服务端写入**：客户端任何途径（请求 body / query / 自定义 header）都不能写入或修改 `owner_id`。PUT body 里的该字段被路由层显式 strip 掉。
3. **`holder_id` 服务端覆盖**：auth-required namespace 上的预约创建一律用 `caller.principal_id` 作为 holder，忽略 body 里的值；release / extend 端点的 path-level holder_id 必须等于 caller.principal_id（admin 例外）。
4. **常量时间比较**：内存 hash 表命中后，最终 `key_hash` 对比走 `hmac.compare_digest`（虽然 dict 查找已防爆破，做 defense-in-depth）。
5. **撤销立即生效**：`revoke_key` 同时移除 `_keys_by_hash` 索引项，下一次 `authenticate(token)` 直接 401，不依赖任何缓存清理。
6. **凭据与业务数据物理隔离**：`auth_data/` 在 `a2x_registry/auth/` 子目录，与 `database/`（可能是 git submodule）完全分开，避免 clone / share 时凭据泄露。

---

## 8. 向前兼容承诺

具体落到三条可测的承诺：

1. **不跑 `auth init`** → 行为与鉴权代码不存在时 byte-equal。回归测试：12 个 legacy 测试文件 + 24 个 test case 在不修改任何源代码的情况下全绿。
2. **创建 dataset 不传 `auth_required`** → 该 namespace 永远匿名，即使注册中心已 bootstrap、即使其他 namespace 已启用 auth。回归测试：[tests/auth/test_anon_namespace_after_init.py](../tests/auth/test_anon_namespace_after_init.py)。
3. **匿名 namespace 的 `api_config.json`** → byte-equal 鉴权启用前的输出（`owner_id` 字段不写入）。回归测试：[tests/auth/test_persistence_roundtrip.py::test_anon_namespace_api_config_has_no_owner_id](../tests/auth/test_persistence_roundtrip.py)。

---

## 9. 客户端凭据解析

[client](../client) SDK 的解析优先级（**有意省略环境变量路径**，避免凭据从父进程意外继承）：

```
1. A2XRegistryClient(api_key="...")     显式构造参数
2. ~/.a2x_registry_client/cli_token.json  配置文件（CLI login 写 / 手写 JSON）
3. None → 仅能调匿名端点（鉴权写操作触发 401）
```

`cli_token.json` 是一个 0600 权限的小 JSON：

```json
{"base_url": "http://127.0.0.1:8000", "api_key": "a2x_pat_..."}
```

由 [a2x_registry_client/auth.py](../client/a2x_registry_client/auth.py) 中的 `resolve_credentials / read_cli_token / write_cli_token / remove_cli_token` 维护；CLI 子命令 `a2x-registry-client login / logout / whoami / keys` 是其上层封装。

错误类型新增两条：`A2XAuthenticationError`（401） / `A2XAuthorizationError`（403），都继承 `A2XHTTPError`。

---

## 10. CLI 工具

### 服务端

```bash
a2x-registry auth init                    # 首次 bootstrap，stderr 打印 admin token
a2x-registry auth reset-admin --confirm   # 销毁现有 principals.json + api_keys.json，重新 bootstrap
```

`auth init` 选项：`--handle` 改 admin 名（默认 `root`）；`--admin-token` 注入指定 plaintext（CI 场景）；`--data-dir` 覆盖 auth_data 位置。

### 客户端

```bash
a2x-registry-client login                 # 交互式 paste token → cli_token.json (chmod 0600)
a2x-registry-client logout                # 删除 cli_token.json
a2x-registry-client whoami                # GET /api/auth/whoami
a2x-registry-client keys list             # 列出自己的 key（admin 可见全部）
a2x-registry-client keys create --name X  # 增发新 key（plaintext 打到 stdout 一次）
a2x-registry-client keys revoke <key_id>  # 撤销自己的 key（admin 可撤销任意）
```

`login` 在 paste 之后会跑一次 `/whoami` 做即时验证，token 无效时立即提示而不是等到第一次业务调用。

---

## 11. 模块依赖图

```
                ┌──────────────────────┐
                │   common/auth_context │  (中立 dataclass，无第三方依赖)
                └──────────┬───────────┘
                           ▲
                ┌──────────┴───────────┐
                ▼                      ▼
    ┌────────────────────┐   ┌──────────────────────┐
    │  register/  service │   │  auth/  store|tokens │
    │              store  │   │       models|errors  │
    │              models │   └──────────┬───────────┘
    └────────┬───────────┘              ▲
             ▲                          │
             │                          │
    ┌────────┴──────────────────────────┴─────┐
    │  backend/  app  startup                  │
    │            routers/ dataset|build|...    │
    │                                          │
    │  auth/  deps  router  cli                │
    └──────────────────────────────────────────┘
```

关键约束：

- `register/*` 永远不 import `auth/*`（grep 验证：0 命中）
- `common/auth_context.py` 永远不 import `fastapi` / `pydantic` 等框架（grep 验证：0 命中）
- `auth/deps.py` 用 lazy import 调 `backend.routers.dataset.get_registry_service`，打破"`auth/` ← `register/` ← `auth/`"的潜在循环

---

## 12. 进一步阅读

- [tests/auth/](../tests/auth/) — 78 个鉴权相关测试，对照本文档每个不变式逐项验证
