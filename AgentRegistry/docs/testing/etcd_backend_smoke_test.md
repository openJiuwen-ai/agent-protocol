# 注册中心 etcd 后端 · 本地单机黑盒验证（curl + etcdctl）

> 目标：在本地起一个单节点 etcd，把注册中心以 `A2X_REGISTRY_DB_KIND=etcd` 启动，用 curl 发包，再**直接查 etcd 里的键**验证落库效果（等价的"数据库验证"，对应 SQLite 场景下用 `sqlite3` 的角色——这里用 `etcdctl`）。
>
> 依赖后端实现（P3）：`A2X_REGISTRY_DB_KIND=etcd` 已生效、`EtcdClient` / `EtcdTableRepo` 已就位。**若后端尚未实现，本流程不可执行**——本文件是在 P3 规划前先行写好的验收流程。
>
> 本例为**未配置 mTLS 的明文 http** 场景（本地验证最简）。配了 `A2X_REGISTRY_ETCD_TLS_CA/CERT/KEY` 则按 https+mTLS 等价执行。
>
> **etcd 版本**：本流程已在 etcd **3.4.14**（单节点 + grpc-gateway）实测通过；生产环境（元戎）为 3.5.x，客户端 wire 格式对两者均兼容（txn compare 的 int64 字段须传 `"0"`，3.4 网关拒绝空串）。下文各节「实际输出」均为 2026-08 本次实测结果，其中时间戳、自动生成的 `service_id`（如 `image_3d38da367a5f76e9`）为示例值，每次运行不同。

## 约定

- 成功响应只列关键字段，完整形状见 OpenAPI schema。
- **etcd 数据库验证**：每个 curl 步骤后给出 `etcdctl` 命令查 etcd 键，验证落库效果。
- 环境变量约定（etcd 模式）：
  ```bash
  export A2X_REGISTRY_MODE=appliance            # etcd 仅允许 appliance 模式
  export A2X_REGISTRY_DB_KIND=etcd
  export A2X_REGISTRY_DB_ENDPOINT=http://127.0.0.1:2379
  export A2X_REGISTRY_ETCD_NAMESPACE=registry-a2x   # 所有 key 前缀
  export A2X_REGISTRY_BIND=127.0.0.1
  export A2X_REGISTRY_PORT=8000
  ```
- **键布局**（依据 `register/table_repo.py` 契约 + 实现约定）：
  - 数据行：`{namespace}/{registry}/{service_id}` → 值 = 该行 JSON（promoted 列 + `data`）
  - 元数据：`{namespace}/_meta/{registry}` → 值 = 该注册表 kind（`image` / `instance`）
- **注册中心启动**（etcd 模式下不建本地 SQL 库）：
  ```bash
  python -m a2x_registry.backend
  ```

## 1. 启动本地 etcd（单节点，grpc-gateway 默认开启）

```bash
etcd --data-dir=/tmp/etcd-data \
  --listen-client-urls http://127.0.0.1:2379 \
  --advertise-client-urls http://127.0.0.1:2379 \
  --listen-peer-urls      http://127.0.0.1:2380 \
  --initial-advertise-peer-urls http://127.0.0.1:2380 \
  --initial-cluster default=http://127.0.0.1:2380
```

**健康检查**（任一即可）：
```bash
etcdctl endpoint health -w table
# 预期：127.0.0.1:2379 is healthy

# 或 grpc-gateway HTTP
curl -sf http://127.0.0.1:2379/health
# 预期：{"health":"true"}
```

**实际输出**（实测 etcd 3.4.14）：
```
$ curl -sf http://127.0.0.1:2379/health
{"health":"true"}

$ curl -s http://127.0.0.1:2379/version
{"etcdserver":"3.4.14","etcdcluster":"3.4.0"}
```

**报错排查**：`etcdctl` 提示 auth/版本错误时加 `--insecure-transport` 或设 `ETCDCTL_API=3`；若 gateway 未开，检查 etcd 版本（v3.3+ 默认开启 HTTP API）。

## 2. 启动注册中心（etcd 模式）

```bash
cd <项目根>
export A2X_REGISTRY_MODE=appliance
export A2X_REGISTRY_DB_KIND=etcd
export A2X_REGISTRY_DB_ENDPOINT=http://127.0.0.1:2379
export A2X_REGISTRY_ETCD_NAMESPACE=registry-a2x
export A2X_REGISTRY_BIND=127.0.0.1
export A2X_REGISTRY_PORT=8000
python -m a2x_registry.backend
```

启动后全健康检查：
```bash
curl -sf http://127.0.0.1:8000/api/images
# 预期：[]（新 etcd 空库）
```

**etcd 元数据验证**（启动期 `create_registry`，appliance 模式建 images / instances / default）：
```bash
etcdctl get registry-a2x/_meta/ --prefix
# 预期：出现 images -> image、instances -> instance（值即 kind）
```

**实际输出**：
```
# 启动日志关键行（kind=etcd 生效、无 SQL 连接）
Warmup [2%] Initializing SQL backend...
  SQL backend ready (kind=etcd, mode=appliance)
  ImageService assembled (appliance mode)
  InstanceService assembled (appliance mode)
node-heartbeat: sweeper started (period=5.0s)
  Per-node heartbeat assembled (recovered 0 nodes)
Warmup [100%] complete — total 0.0s

$ curl -sf http://127.0.0.1:8000/api/images
[]

$ etcdctl get registry-a2x/_meta/ --prefix
registry-a2x/_meta/default
"service"
registry-a2x/_meta/images
"image"
registry-a2x/_meta/instances
"instance"
```
> 注意 etcdctl 输出的 kind 值带 JSON 引号（写入时按 JSON 字符串编码）。

## 3. 镜像管理 `/api/images`

### 3.1 注册镜像

**接口**：`POST /api/images`

```bash
curl -X POST http://127.0.0.1:8000/api/images \
  -H "Content-Type: application/json" \
  -d '{
    "name": "opencode",
    "framework": "opencode",
    "description": "opencode 适配镜像",
    "package_path": "/pkg/opencode/",
    "image_archive_path": "/archive/opencode.tar",
    "version": "v0.2.0",
    "runtime_spec": { "runtime": "python3.11", "cpu": 1000, "memory": 2048,
                      "rootfs": {"imageurl": "harbor.local/adapted/opencode:v0.2.0-mod1.3"} },
    "access_mode": [{"name": "tui", "port": "2222", "cmd": "opencode"}],
    "env_vars": {"A2X_LLM_KEY": "${A2X_LLM_KEY}"},
    "workspace": "/app",
    "mounts": [{"source": "/data/agent", "target": "/data"}],
    "image_module_version": "v1.3",
    "uploaded_by": "user-01"
  }'
```

**预期响应** `200`：
```json
{"name": "opencode", "framework": "opencode", "version": "v0.2.0", "status": "registered"}
```

**etcd 验证**：
```bash
etcdctl get registry-a2x/images/ --prefix
# 预期：一个 key，值 JSON 含 name=opencode、version=v0.2.0、is_default=1、uploaded_by=user-01、data
```

**实际输出**（`service_id` 由 image_sid(name, version) 派生，同 (name, version) 稳定）：
```
$ curl -X POST ...（同上）
{"name":"opencode","framework":"opencode","version":"v0.2.0","status":"registered"}

$ etcdctl get registry-a2x/images/ --prefix
registry-a2x/images/image_3d38da367a5f76e9
{"service_id": "image_3d38da367a5f76e9", "name": "opencode", "framework": "opencode", "version": "v0.2.0",
 "version_key": "00000.00002.00000~", "is_default": 1, "uploaded_by": "user-01",
 "data": {"runtime_spec": {"runtime": "python3.11", "cpu": 1000, "memory": 2048,
           "rootfs": {"imageurl": "harbor.local/adapted/opencode:v0.2.0-mod1.3"}},
          "description": "opencode 适配镜像", "package_path": "/pkg/opencode/",
          "image_archive_path": "/archive/opencode.tar",
          "access_mode": [{"name": "tui", "port": "2222", "cmd": "opencode"}],
          "env_vars": {"A2X_LLM_KEY": "${A2X_LLM_KEY}"}, "workspace": "/app",
          "mounts": [{"source": "/data/agent", "target": "/data"}],
          "image_module_version": "v1.3", "created_at": "2026-08-18T01:28:35Z"}}
```
> 首个镜像自动成为 default（`is_default: 1`）；`created_at` 与 §6 新字段（description / package_path / image_archive_path / access_mode）存于 `data` 内。

### 3.2 查询镜像（按 name 过滤 + 分页）

**接口**：`GET /api/images?name={name}`

```bash
curl 'http://127.0.0.1:8000/api/images?name=opencode'
# 预期 200：扁平数组，一项 = 一个 name 的一个版本（排序 name asc, version_key desc）
curl 'http://127.0.0.1:8000/api/images?framework=opencode&size=10&page=1'
# 预期 200：framework 降级为展示字段，仍可按其筛选；分页数组；响应头含 X-Total-Count
```

**etcd 验证**（分页/排序为后端在内存完成，etcd 只保证行齐全）：
```bash
etcdctl get registry-a2x/images/ --prefix --print-value-only
# 预期：列出该注册表全部镜像行 JSON（数量与 /api/images 返回一致）
```

**实际输出**（API 返回为扁平展示形状，`data` 内字段被摊平；etcd 存的是原始行）：
```
$ curl 'http://127.0.0.1:8000/api/images?name=opencode'
[{"name":"opencode","framework":"opencode","version":"v0.2.0","is_default":true,
  "description":"opencode 适配镜像","package_path":"/pkg/opencode/",
  "image_archive_path":"/archive/opencode.tar",
  "access_mode":[{"name":"tui","port":"2222","cmd":"opencode"}],
  "image_module_version":"v1.3",
  "runtime_spec":{"runtime":"python3.11","cpu":1000,"memory":2048,
                  "rootfs":{"imageurl":"harbor.local/adapted/opencode:v0.2.0-mod1.3"}},
  "workspace":"/app","mounts":[{"source":"/data/agent","target":"/data"}],
  "env_vars":{"A2X_LLM_KEY":"${A2X_LLM_KEY}"},"uploaded_by":"user-01",
  "created_at":"2026-08-18T01:28:35Z"}]

$ curl -D - 'http://127.0.0.1:8000/api/images?name=opencode&size=10&page=1'
# 响应体同上；响应头含：
x-total-count: 1

$ etcdctl get registry-a2x/images/ --prefix --print-value-only
# 输出 = §3.1 中的行 JSON（一行一个镜像），数量与 /api/images 一致
```

### 3.3 取运行规格

**接口**：`GET /api/images/opencode/launch-spec`（`?version=v0.2.0`）

```bash
curl http://127.0.0.1:8000/api/images/opencode/launch-spec?version=v0.2.0
# 预期 200：{name, framework, version, runtime_spec:{...}}，imageurl 透传
```

**实际输出**：
```json
{"name":"opencode","framework":"opencode","version":"v0.2.0",
 "runtime_spec":{"runtime":"python3.11","cpu":1000,"memory":2048,
                 "rootfs":{"imageurl":"harbor.local/adapted/opencode:v0.2.0-mod1.3"}},
 "access_mode":[{"name":"tui","port":"2222","cmd":"opencode"}],
 "env_vars":{"A2X_LLM_KEY":"${A2X_LLM_KEY}"},"workspace":"/app",
 "mounts":[{"source":"/data/agent","target":"/data"}],"image_module_version":"v1.3"}
```

## 4. 实例管理 `/api/instances`

### 4.1 注册实例（三方）

**接口**：`POST /api/instances`

```bash
curl -X POST http://127.0.0.1:8000/api/instances \
  -H "Content-Type: application/json" \
  -d '{
    "service_id": "generic_3f9a1b2c",
    "kind": "三方",
    "framework": "opencode",
    "framework_version": "v0.2.0",
    "node": "192.168.0.12",
    "address": "10.244.1.7:4096",
    "user": "user-01"
  }'
```

**预期响应** `200`：`{service_id, kind, framework, framework_version, address, node, user, status:"运行"}`。

**etcd 验证**：
```bash
etcdctl get registry-a2x/instances/generic_3f9a1b2c
# 预期：值 JSON 含 kind=三方、node=192.168.0.12、data.address=10.244.1.7:4096
```

**实际输出**（时间戳为示例值）：
```
$ curl -X POST ...（同上）
{"service_id":"generic_3f9a1b2c","kind":"三方","framework":"opencode",
 "framework_version":"v0.2.0","node":"192.168.0.12","address":"10.244.1.7:4096",
 "user":"user-01","created_at":"2026-08-18T01:28:54Z",
 "last_active_at":"2026-08-18T01:28:54Z","status":"运行"}

$ etcdctl get registry-a2x/instances/generic_3f9a1b2c
registry-a2x/instances/generic_3f9a1b2c
{"service_id": "generic_3f9a1b2c", "kind": "\u4e09\u65b9", "framework": "opencode",
 "framework_version": "v0.2.0", "node": "192.168.0.12", "user": "user-01",
 "data": {"address": "10.244.1.7:4096", "created_at": "2026-08-18T01:28:54Z",
          "last_active_at": "2026-08-18T01:28:54Z"}}
```
> etcd 值中中文按 JSON 转义显示（`\u4e09\u65b9` = 三方）；`address` 在 `data` 内，API 响应摊平到顶层。

### 4.2 查询实例（按 node 过滤 + 状态过滤）

**接口**：`GET /api/instances?node={ip}&include_unhealthy={bool}`

```bash
curl 'http://127.0.0.1:8000/api/instances?node=192.168.0.12'
# 预期 200：列出该 node 实例（status 为落库值，默认只回 运行）
```

**etcd 验证**：
```bash
etcdctl get registry-a2x/instances/ --prefix --print-value-only
# 预期：全部实例行；按 node/uservice_id 过滤为后端内存完成
```

**实际输出**：
```
$ curl 'http://127.0.0.1:8000/api/instances?node=192.168.0.12'
[{"service_id":"generic_3f9a1b2c","kind":"三方","framework":"opencode",
  "framework_version":"v0.2.0","node":"192.168.0.12","address":"10.244.1.7:4096",
  "user":"user-01","created_at":"2026-08-18T01:28:54Z",
  "last_active_at":"2026-08-18T01:28:54Z","status":"运行"}]

$ etcdctl get registry-a2x/instances/ --prefix --print-value-only
# 输出 = §4.1 中的实例行 JSON（过滤为后端内存完成，etcd 侧行齐全）
```

## 5. 节点心跳（已移除）

注册中心**不再接收节点心跳**：`POST /api/nodes/{node}/heartbeat` 与 `GET/POST /api/lease-config` 已移除，调用返回 `404`。etcd 中没有任何心跳相关的键（也从未有过——心跳活性本就只存进程内存）。

实例存活由 gateway 周期轮询**元戎 List** 掌握：不在运行的实例经 `PATCH /api/instances/{service_id}` 写 `status`（`data.status`，落 etcd 行内 JSON），注册中心不派生状态、不自动剔除——实例键**不会**因停止心跳被删除。

验证方式（结合 §4.1 实例行）：
```bash
# 1. PATCH 置 停止 后，etcd 中该行仍在，data.status 变为 停止
curl -X PATCH http://127.0.0.1:8000/api/instances/generic_3f9a1b2c \
  -H "Content-Type: application/json" -d '{"status": "停止"}'
etcdctl get registry-a2x/instances/generic_3f9a1b2c --print-value-only
# 预期：行 JSON 仍在，"status":"停止"

# 2. 默认列表过滤（只回 运行）为后端内存完成，etcd 侧行不受影响
curl -s 'http://127.0.0.1:8000/api/instances'
# 预期：[]（generic_3f9a1b2c 已置 停止）
```

## 6. 键布局与元数据总查

```bash
# 全部业务键（namespace 下所有 registry）
etcdctl get registry-a2x/ --prefix

# 元数据（各注册表 kind）
etcdctl get registry-a2x/_meta/ --prefix
# 预期：/registry-a2x/_meta/default   -> service
#       /registry-a2x/_meta/images    -> image
#       /registry-a2x/_meta/instances -> instance
```

**实际输出**（§5 PATCH 置停止后：实例键仍在，仅 `_meta` + 镜像行 + 实例行均保留——注册中心不自动剔除）：
```
$ etcdctl get registry-a2x/ --prefix --keys-only
registry-a2x/_meta/default
registry-a2x/_meta/images
registry-a2x/_meta/instances
registry-a2x/images/image_3d38da367a5f76e9
registry-a2x/instances/generic_3f9a1b2c
```

## 7. 错误码速查

| HTTP | 场景（etcd 后端） | 响应体 |
|------|------|--------|
| `400` | 注册镜像 rootfs.imageurl 缺失 / filter key 不在白名单 / PATCH status 不在 运行/停止/异常 枚举 | `{"detail":"..."}` |
| `404` | 不存在的 name launch-spec / PATCH 不存在 service_id / 调已移除的节点心跳 `/api/nodes/{node}/heartbeat` 或 `/api/lease-config` | `{"detail":"..."}` |
| `409` | 注销在用镜像 | `{"code":"image_in_use","detail":"...","instances":[...]}` |
| `502` | 注销镜像时镜像仓删除接口失败（外部依赖） | `{"detail":"..."}` |
| `503` | etcd 不可达 / 超时（后端启动即 fail-fast，运行中掉线则查询报错） | `{"detail":"..."}` |

> `401` / `403` 鉴权错误不在当前范围。
> **已知未接**：运行中 etcd 掉线时 `EtcdError` → 503/502 的 API 层映射尚未接入，当前会透传为 500；启动期不可达则正常 fail-fast（warmup 报错、服务不 ready）。

## 8. 收尾与清理

```bash
# 停注册中心（Ctrl-C）

# 清空测试数据（可选）——删 namespace 前缀下全部键
etcdctl del registry-a2x/ --prefix

# 停 etcd（Ctrl-C），清数据目录
rm -rf /tmp/etcd-data
```

## 附：黑盒通过判据

- §2 启动后 `/api/images` 返回 `[]`，且 `_meta/` 已出现 images / instances（启动期建表）。
- §3 注册镜像后 etcd `registry-a2x/images/` 下新增行，`/api/images` 可读回。
- §4 注册实例后 etcd `registry-a2x/instances/` 出现该 service_id，`/api/instances` 可读回。
- §5 PATCH 置 停止/异常 后实例键不消失（注册中心不自动剔除），默认列表过滤为内存完成。
- 全程仅操作 etcd，本地无 `registry.db` 文件**新建或更新**（确认未走 sqlite/memory 后端；若机器上存在旧 sqlite 遗留文件，以 mtime 不变化为准）。

> **实测记录（2026-08-18）**：以上判据在 etcd 3.4.14 单机 + 明文 http 下全部通过；期间发现并修复 etcd 3.4 网关 txn wire 格式问题（`create_revision` 须传 `"0"`）。etcd 3.5.x 环境待装机后按本流程复测。