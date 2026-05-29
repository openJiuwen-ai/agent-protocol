# A2X 心跳保活模块设计

> 适用 v0.1.8+。本文件面向运维 / 开发者。**基本用法**见 [README.md](../README.md) 的相关章节；本文聚焦设计原理、文件布局、模块依赖与状态机不变式。

## 1. 定位与设计原则

注册中心默认行为：服务一旦注册，永久存在直到显式 `deregister`。这会让 crash / OOM / 网络断的服务在注册表里留下"幽灵条目"。

心跳模块为每个 service 引入一个**带 TTL 的租约**：客户端必须周期性续约，否则 sweeper 把服务从可见列表中剔除（mark unhealthy），grace period 之后真正删除。设计严格遵守 A2X 项目的两条铁律：

| 层级 | 默认 | 启用方式 | 行为 |
|------|------|---------|------|
| 注册中心整体 | 心跳模块**始终加载** | — | 仅当 namespace 显式启用 + 客户端显式声明 TTL 时才生成 lease；其他情况零开销 |
| Namespace | `lease_config.enabled=false` | admin POST `/api/datasets/{ds}/lease-config` | 默认 namespace 永远不接受 `lease_ttl`；既有数据集与既有客户端零影响 |

**三档租约状态**：

| 状态 | 触发 | 对外行为 |
|------|------|---------|
| HEALTHY | 注册成功 / 收到心跳 | `list_services` 出现；`reserve` 可选；`get_agent` 正常 |
| UNHEALTHY | TTL 过期 | `list_services` 默认过滤（`?include_unhealthy=true` 才可见）；`reserve` 跳过；`get_agent` 仍返回 |
| HARD-DELETED | UNHEALTHY 持续 `grace_period` | sid 彻底消失，`api_config.json` 重写 |

**关键不变式**：心跳过期不直接删除，留出 grace 窗口；任何阶段收到心跳都回到 HEALTHY；硬删走和 admin DELETE 一模一样的代码路径（无第二条删除分支）。

## 2. 数据模型

### 2.1 `RegistryEntry.lease_ttl`（持久化）

[a2x_registry/register/models.py](../a2x_registry/register/models.py) 的 `RegistryEntry` 新增字段：

```python
class RegistryEntry(BaseModel):
    ...
    lease_ttl: Optional[int] = None
```

| 取值 | 含义 |
|------|------|
| `None` | 永久服务 / 来自 `user_config.json` / 注册时 namespace 没启用心跳 / 客户端没传 TTL |
| `int > 0` | 客户端注册时声明的 TTL（秒）；服务端用它来计算续约后的新 `expires_at` |

**为什么必须持久化**：注册中心重启后面对一个已注册的服务，必须知道"客户端期望我用多长 TTL 来续约"。否则下次心跳来了不知道把 `expires_at` 设到多远。

**序列化策略**：`None` 值**不写入** `api_config.json`（omit-when-None）—— 这样心跳启用前注册的服务的输出与新版本 byte-equal，向前兼容 lockfile 测试依赖此。

### 2.2 `HeartbeatLease`（内存）

[a2x_registry/heartbeat/models.py](../a2x_registry/heartbeat/models.py)：

```python
@dataclass
class HeartbeatLease:
    ttl_seconds: int                  # 来自 RegistryEntry.lease_ttl
    grace_period_seconds: int         # 来自 namespace 的 lease_config
    expires_at: float                 # monotonic 时钟：HEALTHY → UNHEALTHY 时刻
    grace_deadline: float             # monotonic：UNHEALTHY → 硬删时刻
    last_heartbeat_at: float          # monotonic
    state: HBState                    # HEALTHY | UNHEALTHY
```

**为什么不持久化运行时状态**：① `expires_at` 每次心跳都变 → 持久化等于把心跳成本放大成磁盘 I/O；② 重启后跨时间是脏的（磁盘上的值实际已过期）；③ 重启已经有 grace_period 兜底机制。

**重启恢复**：[a2x_registry/backend/startup.py](../a2x_registry/backend/startup.py) 在 warmup 中读取所有 `lease_ttl != None` 的 entry，调 `HeartbeatStore.recover_from_persisted`，给每个 entry 重新植入一个 UNHEALTHY 状态的 lease，`grace_deadline = now + grace_period`。客户端有一个 grace 窗口重新心跳，否则 sweeper 硬删。

## 3. Per-namespace 配置

```
database/<namespace>/
├── service.json
├── api_config.json
├── register_config.json
├── vector_config.json
├── auth_config.json
├── lease_config.json     # 新增 —— 缺失即 enabled=false
└── ...
```

`lease_config.json` schema（[a2x_registry/register/store.py](../a2x_registry/register/store.py) `LEASE_CONFIG_DEFAULT`）：

```json
{
  "enabled": true,
  "min_ttl": 10,
  "max_ttl": 3600,
  "grace_period": 300,
  "schema_version": 1
}
```

读取走 `RegistryStore.load_lease_config()` → `RegistryService.get_lease_config()`（lazy 缓存）。写入走 admin-or-anon 网关：未启用 auth 模块的注册中心允许任何调用方配置（开发友好）；启用 auth 后必须 admin 角色。`set_lease_config` 同步缓存失效。

## 4. 四角矩阵

客户端注册 (`POST /api/datasets/{ds}/services/a2a`) 时可以传 `lease_ttl`。组合矩阵：

| namespace `enabled` | client `lease_ttl` | 结果 |
|---|---|---|
| ❌ | ❌ | ✅ 永久服务（向前兼容） |
| ❌ | ✅ | 400 `heartbeat_not_supported` |
| ✅ | ❌ | 400 `ttl_required` + 响应 body 携带 `min_ttl` / `max_ttl` |
| ✅ | ✅ in range | ✅ 200 + 响应含 `lease_ttl` / `lease_expires_at` |
| ✅ | ✅ out of range | 400 `ttl_out_of_range` + 响应含 `min_ttl` / `max_ttl` |

校验逻辑集中在 [a2x_registry/heartbeat/store.py](../a2x_registry/heartbeat/store.py) 的 `HeartbeatStore.validate()` —— 纯检查、无副作用。注册流程：

```
1. router 调 store.validate(ds, ttl)     # 4-corner 矩阵；可能 raise HeartbeatError
2. router 调 svc.register_a2a(req, ctx)  # 落 RegistryEntry，lease_ttl 字段写入磁盘
3. router 调 store.install(ds, sid, ttl) # 在内存 HeartbeatStore 安装 lease（健康态）
4. router 返回 RegisterResponse{..., lease_ttl, lease_expires_at}
```

**先 validate，再 register，再 install**：validate 没副作用所以可前置；register 成功后才 install lease，避免"注册失败但留下幽灵 lease"。

错误响应统一通过 `_run` 的 `HeartbeatError → 400` 分支，body 形如：

```json
{
  "detail": {
    "code": "ttl_out_of_range",
    "detail": "lease_ttl=999 out of range [10, 3600] for namespace 'foo'.",
    "min_ttl": 10,
    "max_ttl": 3600
  }
}
```

客户端 SDK 用 `code` 字段派发到 `A2XTTLOutOfRangeError` / `A2XTTLRequiredError` / `A2XHeartbeatNotSupportedError`。

## 5. 端点

### 5.1 配置

| 方法 | 路径 | 鉴权 | 说明 |
|------|------|------|------|
| GET | `/api/datasets/{ds}/lease-config` | 匿名 | 读 namespace 配置，SDK 探测用 |
| POST | `/api/datasets/{ds}/lease-config` | admin-or-anon | 启用 / 改 / 关 |

### 5.2 心跳

| 方法 | 路径 | 鉴权 | 说明 |
|------|------|------|------|
| POST | `/api/datasets/{ds}/services/{sid}/heartbeat` | 跟随 namespace 的 `authorize` 策略 | 续约；可选 body `{status?}` 顺手更新 `agent_card.status` |
| DELETE | `/api/datasets/{ds}/services/{sid}/heartbeat` | 同上 | 默认软撤销（mark unhealthy + 走 grace）；`{permanent: true}` 直接硬删 |

注册端点本身（`POST /services/a2a` / `/services/generic`）在 request body 上新增 `lease_ttl` 可选字段，响应 body 新增 `lease_ttl` / `lease_expires_at`。其他既有字段不变。

## 6. List / Reserve 的过滤

新增 query 参数 `include_unhealthy`，默认 `false`：

```
GET /api/datasets/{ds}/services?include_unhealthy=false   # 默认，过滤掉 UNHEALTHY
GET /api/datasets/{ds}/services?include_unhealthy=true    # 运维查全部（含 UNHEALTHY，不含 HARD-DELETED）
```

`include_unhealthy` 与既有 `include_leased` **独立**、可叠加；一个服务可能同时被预约且心跳过期。

`POST /reservations` 自动跳过 unhealthy 候选——你不想锁住一个反正不可达的服务。

实现：[a2x_registry/register/service.py](../a2x_registry/register/service.py) 的 `RegistryService.is_unhealthy(ds, sid) -> bool` 是一个回调代理，默认实现是 `lambda d, s: False`（heartbeat 模块未加载时）；backend startup 把 `HeartbeatStore.is_unhealthy` 注入进来。Router 在 list/reserve 循环里调 `svc.is_unhealthy(...)`。**`register/` 永远不 import `heartbeat/`**。

## 7. Sweeper

[a2x_registry/heartbeat/sweeper.py](../a2x_registry/heartbeat/sweeper.py)：单一后台 daemon thread，周期 5s（生产默认）。每次 tick：

```
newly_unhealthy, to_hard_delete = store.sweep_tick(now=time.monotonic())
for (ds, sid) in to_hard_delete:
    registry_svc.deregister(ds, sid, caller=SYSTEM_CTX)
```

`SYSTEM_CTX` 是合成的 admin `AuthContext`（[a2x_registry/heartbeat/system_ctx.py](../a2x_registry/heartbeat/system_ctx.py)），让 sweeper 走和"admin 手动 DELETE"完全相同的代码路径。任何 sweeper 异常都被 try/except 包住记 warning，**不会让 daemon 线程崩溃**。

测试驱动方式：`sweep_once()` 暴露给单元测试，可以同步驱动状态机；`sweep_tick(now=...)` 接受外部时间钟避免 sleep。

## 8. 模块依赖图

```
                        ┌────────────────────────────┐
                        │  common/auth_context (中立) │  (auth & heartbeat 都用)
                        └────────────────────────────┘

    ┌──────────────────────────┐       set_unhealthy_check
    │  register/  service       │ ◄──────────callback────────┐
    │             store         │                              │
    │             models        │                              │
    │             (没有 import  │                              │
    │              heartbeat/) │                              │
    └────────────┬─────────────┘                              │
                 ▲                                             │
                 │ deregister(SYSTEM_CTX)                     │
                 │                                             │
    ┌────────────┴─────────────┐         ┌──────────────────┴─────┐
    │  backend/  routers/      │         │  heartbeat/  store      │
    │            dataset       │ ──────► │              sweeper    │
    │            heartbeat     │  read+  │              router     │
    │            startup       │  install│              deps       │
    └──────────────────────────┘         └─────────────────────────┘
```

**关键 invariants**:
- `register/*` 永远不 import `heartbeat/*`（grep 验证：0 命中）
- `heartbeat/store.py` 不知道 FastAPI / HTTP / 路由（pure Python，可单测）
- 一条 callback 连接两侧：`registry_svc.set_unhealthy_check(heartbeat_store.is_unhealthy)`

## 9. 客户端 SDK

客户端 SDK 的心跳模块提供：

- `HeartbeatRenewer` —— per-(ds, sid) daemon thread，每 `ttl/3` 秒发一次心跳，指数退避，daemon=True 不阻塞进程退出
- `HeartbeatRegistry` —— per-client 管理器
- `AsyncHeartbeatRenewer` / `AsyncHeartbeatRegistry` —— 异步客户端用 `asyncio.Task`

SDK 公开方法：

```python
client.register_agent(..., lease_ttl=60, auto_renew=True)  # 注册即起后台续约
client.heartbeat(ds, sid, status=None)                       # 手动续约
client.drain(ds, sid)                                        # status=offline，保留 entry
client.shutdown(*, permanent=False, ...)                     # 批量撤销 lease 或硬删
client.close()                                                # 停所有 renewer，关 transport
```

所有 lifecycle hook 都是 **opt-in**：默认 `auto_renew=False`、`auto_deregister=False`、不装 SIGTERM/atexit handler，避免与业务方的信号处理冲突。

## 10. 三类 TTL 在 A2X 里彼此独立

A2X 现在有三类 TTL，**互不耦合**：

| 类别 | 实例 | 谁配置 | 持有者 | 失效后果 |
|------|------|--------|--------|---------|
| **独占锁** | reservation lease | 调用方 reserve 时传 | 任意 holder | 锁释放，他人可抢 |
| **存活租约** | service heartbeat（本文档） | 客户端注册时传，namespace 限 `[min, max]` | 服务本身 | mark unhealthy → 硬删 |
| **过期权限** | ApiKey.expires_at（Phase 1 占位） | admin 创建 key 时传 | API key | 凭据失效 |

底层数据结构 `_Lease` (reservation) 和 `HeartbeatLease` 形状相似但**有意保持分开**：
- 端点分开：`/reservations/...` vs `/services/{sid}/heartbeat`
- 配置文件分开：namespace 用 `lease_config.json` 控制心跳；reservation 用 request body
- Sweeper 分开：reservation 懒扫描；心跳后台 daemon
- 审计事件分开

**Refactor 建议（不在本 Phase 实施）**：未来若引入第 3 类后台 lease（如 API-key 自动过期），可以提炼一个公共 `Lease` 基类与统一 sweeper。当前 ROI 低（约一文件、两行 save）。

## 11. 向前兼容承诺

具体落到可测的承诺：

1. **没启用 lease_config 的 namespace** → 行为与心跳模块未存在时 byte-equal。回归测试：[tests/heartbeat/test_back_compat.py](../tests/heartbeat/test_back_compat.py)，全部既有 server 测试不修改任何源码全绿。
2. **注册时不传 `lease_ttl`** → 服务永久，`api_config.json` 的 entry 不写 `lease_ttl` 字段（byte-equal pre-heartbeat output）。回归测试同上。
3. **既有 SDK 客户端** → 不传 `lease_ttl` / 不调 `heartbeat()` / 不使用 `auto_renew`，注册端点行为不变；额外的 `lease_ttl` / `lease_expires_at` 响应字段对旧客户端是无害的（Pydantic 忽略未知字段）。

## 12. 进一步阅读

- [tests/heartbeat/](../tests/heartbeat/) —— server 测试，对照本文档每个不变式逐项验证
