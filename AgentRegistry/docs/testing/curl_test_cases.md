# 注册中心 API · 手工测试 curl 命令合集

> 基于 registry_openapi.yaml，覆盖一体机场景全部接口。
> 默认服务器：`http://127.0.0.1:8000`。
> 命令可直接复制到 bash 运行；每个接口选 1-2 个典型参数组合。

## 约定

- 成功响应只列关键字段，完整形状见 OpenAPI schema。
- 错误路径在每个接口末尾标注典型一种。
- 联调场景见末尾 §6。
- **数据库验证（SQLite）**：每个 curl 步骤后给出 `sqlite3` 命令，直接查 `$A2X_REGISTRY_DB` 验证落库效果。环境变量约定：
  ```bash
  # 730 单机 SQLite，路径由 registry.env 的 A2X_REGISTRY_HOME 决定
  export A2X_REGISTRY_DB="${A2X_REGISTRY_HOME:-/var/lib/a2x-registry}/registry.db"
  ```
- 表结构真源：[a2x_registry/common/db.py](../../a2x_registry/common/db.py) 的 `SCHEMA_SQL`（4 表：`registry_meta` / `service` / `image` / `instance`）。
- **实例 status 落库（data JSON）**：`instance` 表无 `status` 列，status 存在行内 `data` JSON（`data.status`，注册默认 `运行`，gateway 经 PATCH 写入 `停止`/`异常`/`运行`）；查询时 node 心跳 UNHEALTHY 会把该 node 实例派生覆盖为 `异常`。
- **心跳活性不入库**（内存态）：§3 的验证只能查 `instance` 表是否被 sweeper 剔除，不能直接查心跳本身。
- **租约配置不入库**（内存态）：§4 的 `lease-config` 仅存进程内存（`NodeHeartbeatStore._config`），重启后恢复默认值；验证只能通过 API 读对照，不能用 `sqlite3` 查 `registry_meta`。

## 构建二进制
以下命令在代码仓根目录执行。

```bash
uv sync
source .venv/bin/activate
# 加载环境变量
source ./build_test/registry.env

# 启动注册中心
a2x-registry
```

如果需要构建二进制，执行 `uv build` 即可，或者使用以下Pyinstaller命令。
```bash
# uv add pyinstaller
pyinstaller \
  --onefile \
  --name a2x-registry \
  --collect-submodules a2x_registry \
  --hidden-import uvicorn.logging \
  --hidden-import uvicorn.protocols.http.auto \
  --hidden-import uvicorn.protocols.websockets.auto \
  --hidden-import uvicorn.lifespan.on \
  --hidden-import uvicorn.loop \
  a2x_registry/backend/__main__.py
```

---

## 1. 镜像管理 `/api/images`

### 1.1 注册镜像

**接口**：`POST /api/images`
**场景**：镜像处理模块适配完成后，登记引用 + 元戎运行规格。

```bash
curl -X POST http://127.0.0.1:8000/api/images \
  -H "Content-Type: application/json" \
  -d '{
    "framework": "opencode",
    "framework_version": "v0.2.0",
    "runtime_spec": {
      "runtime": "python3.11",
      "sandbox_type": "docker",
      "rootfs": {
        "imageurl": "harbor.local/adapted/opencode:v0.2.0-mod1.3",
        "user": "agentos",
        "ports": ["tcp:8080"]
      },
      "cpu": 1000,
      "memory": 2048,
      "ports": [{"port": 8080, "protocol": "tcp"}]
    },
    "env_vars": {"A2X_LLM_KEY": "${A2X_LLM_KEY}"},
    "workspace": "/app",
    "mounts": [{"source": "/data/agent", "target": "/data"}],
    "image_module_version": "v1.3",
    "uploaded_by": "user-01"
  }'
```

**预期响应** `200`：
```json
{"framework": "opencode", "framework_version": "v0.2.0", "is_default": true, "status": "registered"}
```

**效果**：按 `framework + framework_version` 幂等 upsert；该 framework 首次注册时自动置为默认版本。`runtime_spec` 为不透明 JSON 透传，注册中心不解析其内部字段。
**错误**：`runtime_spec` 缺失 -> `422`（pydantic 校验错误）。

**数据库验证**：
```bash
# 1. image 表新增一行（registry='images'），data JSON 含 runtime_spec 透传
sqlite3 "$A2X_REGISTRY_DB" \
  "SELECT framework, framework_version, is_default,
          json_extract(data,'$.runtime_spec.rootfs.imageurl') AS imageurl,
          json_extract(data,'$.runtime_spec.cpu') AS cpu
   FROM image WHERE registry='images' AND framework='opencode';"
# 预期：opencode|v0.2.0|1|harbor.local/adapted/opencode:v0.2.0-mod1.3|1000

# 2. registry_meta 已登记 'images'（启动期 create_registry）
sqlite3 "$A2X_REGISTRY_DB" \
  "SELECT registry, kind FROM registry_meta WHERE registry='images';"
# 预期：images|image
```

### 1.2 查询镜像（按 framework 过滤）

**接口**：`GET /api/images?framework={fw}`

```bash
curl 'http://127.0.0.1:8000/api/images?framework=opencode'
```

**预期响应** `200`：
```json
[{
  "framework": "opencode",
  "framework_version": "v0.2.0",
  "is_default": true,
  "image_module_version": "v1.3",
  "runtime_spec": {"runtime": "python3.11", "rootfs": {"imageurl": "..."}, "cpu": 1000, ...},
  "workspace": "/app",
  "mounts": [{"source": "/data/agent", "target": "/data"}],
  "env_vars": {"A2X_LLM_KEY": "${A2X_LLM_KEY}"},
  "uploaded_by": "user-01",
  "created_at": "2026-07-06T10:00:00Z"
}]
```

**效果**：扁平数组返回（一条目 = 一个框架版本），`runtime_spec` 为不透明透传。不传 `?framework=` 返回全部。支持 `?size` / `?page` 分页。

**数据库验证**：
```bash
# 对照 image 表中该 framework 全部版本行 + 默认版本指针
sqlite3 "$A2X_REGISTRY_DB" \
  "SELECT framework_version, is_default,
          json_extract(data,'$.image_module_version') AS mod_ver
   FROM image WHERE registry='images' AND framework='opencode'
   ORDER BY is_default DESC, framework_version;"
# 预期首行：v0.2.0|1|v1.3（is_default=1 排在前）
```

### 1.3 取运行规格（gateway 拉起前调用）

**接口**：`GET /api/images/{framework}/launch-spec?version={ver}`

```bash
# 默认版本
curl http://127.0.0.1:8000/api/images/opencode/launch-spec

# 指定版本
curl 'http://127.0.0.1:8000/api/images/opencode/launch-spec?version=v0.2.0'
```

**预期响应** `200`：
```json
{
  "framework": "opencode",
  "framework_version": "v0.2.0",
  "runtime_spec": {
    "runtime": "python3.11",
    "sandbox_type": "docker",
    "rootfs": {
      "imageurl": "harbor.local/adapted/opencode:v0.2.0-mod1.3",
      "user": "agentos",
      "ports": ["tcp:8080"]
    },
    "cpu": 1000,
    "memory": 2048,
    "ports": [{"port": 8080, "protocol": "tcp"}]
  },
  "env_vars": {"A2X_LLM_KEY": "${A2X_LLM_KEY}"},
  "workspace": "/app",
  "mounts": [{"source": "/data/agent", "target": "/data"}],
  "image_module_version": "v1.3"
}
```

**效果**：返回元戎运行规格（`runtime_spec` 不透明透传 + `env_vars`/`workspace`/`mounts` 顶层字段）；不带 version 取默认版本。
**错误**：framework 或版本不存在 -> `404 {"detail":"..."}`。

**数据库验证**：
```bash
# 默认版本路径：查 is_default=1 那一行的 data
sqlite3 "$A2X_REGISTRY_DB" \
  "SELECT framework_version,
          json_extract(data,'$.runtime_spec.rootfs.imageurl') AS imageurl,
          json_extract(data,'$.runtime_spec.cpu') AS cpu
   FROM image
   WHERE registry='images' AND framework='opencode' AND is_default=1;"
# 预期：v0.2.0|harbor.local/adapted/opencode:v0.2.0-mod1.3|1000
```

### 1.4 设默认版本

**接口**：`PUT /api/images/{framework}/default`

```bash
# 假设此前已注册 opencode 的 v0.1.0 与 v0.2.0 两个版本
curl -X PUT http://127.0.0.1:8000/api/images/opencode/default \
  -H "Content-Type: application/json" \
  -d '{"framework_version": "v0.2.0"}'
```

**预期响应** `200`：
```json
{"framework": "opencode", "default": "v0.2.0", "status": "updated"}
```

**效果**：清该 framework 旧 `is_default`、置新版为 1。
**错误**：framework 不存在 -> `404`。

**数据库验证**：
```bash
# 同 framework 下应恰有一行 is_default=1（新版），其他均为 0
sqlite3 "$A2X_REGISTRY_DB" \
  "SELECT framework_version, is_default
   FROM image WHERE registry='images' AND framework='opencode'
   ORDER BY framework_version;"
# 预期（两版本场景）：v0.1.0|0  /  v0.2.0|1
```

### 1.5 注销镜像

**接口**：`DELETE /api/images/{framework}/{version}`

```bash
curl -X DELETE http://127.0.0.1:8000/api/images/opencode/v0.2.0
```

**预期响应** `200`：
```json
{"framework": "opencode", "framework_version": "v0.2.0", "status": "deregistered"}
```

**效果**：先校验无在用实例 -> 删镜像仓文件 -> 删条目（删的是默认版本则把最新版补为默认）。
**错误**：仍有在用实例 -> `409 {"detail":"image opencode@v0.2.0 still has N in-use instance(s); cannot deregister"}`。

**数据库验证**：
```bash
# 1. image 表对应行已删除
sqlite3 "$A2X_REGISTRY_DB" \
  "SELECT COUNT(*) FROM image
   WHERE registry='images' AND framework='opencode' AND framework_version='v0.2.0';"
# 预期：0

# 2. 若删的是默认版本，应自动补一个新默认（同 framework 下剩余行恰一行 is_default=1）
sqlite3 "$A2X_REGISTRY_DB" \
  "SELECT COUNT(*) FROM image
   WHERE registry='images' AND framework='opencode' AND is_default=1;"
# 预期：1（如还有其他版本）或 0（如该 framework 已无任何版本）
```

## 2. 实例管理 `/api/instances`

### 2.1 注册实例（三方）

**接口**：`POST /api/instances`
**场景**：gateway 拿 launch-spec、调元戎拉起后，带落点注册。

```bash
curl -X POST http://127.0.0.1:8000/api/instances \
  -H "Content-Type: application/json" \
  -d '{
    "service_id": "generic_3f9a1b2c",
    "kind": "三方",
    "framework": "opencode",
    "framework_version": "v0.2.0",
    "node": "192.168.0.12",
    "instance_id": "yr-inst-7f3a92",
    "address": "10.244.1.7:4096",
    "user": "user-01"
  }'
```

**预期响应** `200`：
```json
{
  "service_id": "generic_3f9a1b2c",
  "kind": "三方", "framework": "opencode", "framework_version": "v0.2.0",
  "address": "10.244.1.7:4096", "node": "192.168.0.12", "user": "user-01",
  "instance_id": "yr-inst-7f3a92",
  "created_at": "2026-07-06T10:00:00Z",
  "last_active_at": "2026-07-06T10:00:00Z",
  "status": "运行"
}
```

**效果**：`service_id` 幂等 upsert；重发即覆盖。`service_id` 由 `instance_sid(user, framework)` 派生（每用户每框架一个实例）。`instance_id` 为元戎实例 ID（gateway 拉起后回填；非元戎拉起可空、不做主键）；`status` 注册即 `运行`（落库 `data.status`）。
**错误**：缺 `node` 等必填字段 -> `400`（pydantic `422`）；`kind` 非 `三方`/`九问` -> `400`。

**数据库验证**：
```bash
# 1. instance 表新增一行（registry='instances'），data JSON 含 address/instance_id/status
sqlite3 "$A2X_REGISTRY_DB" \
  "SELECT service_id, kind, framework, framework_version, node, \"user\",
          json_extract(data,'\$.address') AS address,
          json_extract(data,'\$.instance_id') AS instance_id,
          json_extract(data,'\$.status') AS status,
          json_extract(data,'\$.created_at') AS created_at
   FROM instance
   WHERE registry='instances' AND service_id='generic_3f9a1b2c';"
# 预期：generic_3f9a1b2c|三方|opencode|v0.2.0|192.168.0.12|user-01|10.244.1.7:4096|yr-inst-7f3a92|运行|2026-07-06T10:00:00Z

# 2. registry_meta 已登记 'instances'
sqlite3 "$A2X_REGISTRY_DB" \
  "SELECT registry, kind FROM registry_meta WHERE registry='instances';"
# 预期：实例注册表|instance
```

### 2.2 注册实例（九问）

```bash
curl -X POST http://127.0.0.1:8000/api/instances \
  -H "Content-Type: application/json" \
  -d '{
    "service_id": "generic_9c21d4e5",
    "kind": "九问",
    "framework": "jiuwen-report",
    "framework_version": "v1.0.0",
    "node": "192.168.0.11",
    "instance_id": "yr-inst-2b5c41",
    "address": "10.244.2.3:8080",
    "user": "user-02"
  }'
```

**效果**：九问流程与三方一致，仅 `kind=九问`、framework 来自一体机预置镜像注册表。不带 `instance_id`（非元戎拉起）也可注册，回执 `instance_id` 为空串。

**数据库验证**：
```bash
# instance 表中 kind='九问' 行
sqlite3 "$A2X_REGISTRY_DB" \
  "SELECT service_id, framework, framework_version, node, \"user\",
          json_extract(data,'\$.instance_id') AS instance_id
   FROM instance WHERE registry='instances' AND kind='九问';"
# 预期：generic_9c21d4e5|jiuwen-report|v1.0.0|192.168.0.11|user-02|yr-inst-2b5c41
```

### 2.3 查询实例（按 node 过滤 + 含异常）

**接口**：`GET /api/instances?node={ip}&include_unhealthy={bool}`

```bash
# 默认只回运行中
curl http://127.0.0.1:8000/api/instances

# 按节点过滤 + 含异常（运维诊断用）
curl 'http://127.0.0.1:8000/api/instances?node=192.168.0.12&include_unhealthy=true'
```

**效果**：`status` 为落库字段（`data.status`：运行 / 停止 / 异常，gateway 经 PATCH 写入），node 心跳 UNHEALTHY 时派生覆盖为 `异常`；`include_unhealthy=false` 默认只回 `运行`（`停止`/`异常` 均被过滤，SQL 下推 `COALESCE(json_extract(data,'$.status'),'运行')='运行'`）。
**支持的 filter key**：`include_unhealthy` / `node` / `framework` / `kind` / `user`（白名单，其他 key 返回 `400`）。

**数据库验证**：
```bash
# 对照 instance 表中该 node 上的全部实例（status 落库在 data JSON）
sqlite3 "$A2X_REGISTRY_DB" \
  "SELECT service_id, kind, framework, node, \"user\",
          json_extract(data,'\$.address') AS address,
          json_extract(data,'\$.status') AS status
   FROM instance WHERE registry='instances' AND node='192.168.0.12';"
# 预期：列出该 node 全部实例（含 data.status='停止'/'异常' 但默认查询被过滤的行；超 grace_period 被剔除后此处为空）

# 索引命中校验（EXPLAIN QUERY PLAN 应走 idx_instance_node）
sqlite3 "$A2X_REGISTRY_DB" \
  "EXPLAIN QUERY PLAN
   SELECT * FROM instance WHERE registry='instances' AND node='192.168.0.12';"
# 预期：SEARCH ... USING INDEX idx_instance_node (registry=? AND node=?)
```

### 2.4 变更实例（元戎迁移后 / 状态更新）

**接口**：`PATCH /api/instances/{service_id}`
**场景**：gateway 在元戎迁移、node/address/instance_id 改变时更新；或据元戎 List 置 `status`（`停止`/`异常`/`运行`）。四个字段至少给一个；`service_id` 不变。

```bash
# 场景 A：元戎迁移（node + address + instance_id 回填）
curl -X PATCH http://127.0.0.1:8000/api/instances/generic_3f9a1b2c \
  -H "Content-Type: application/json" \
  -d '{"node": "192.168.0.20", "address": "10.244.3.9:4096", "instance_id": "yr-inst-9d1e07"}'
```

```bash
# 场景 B：状态更新（gateway 据元戎 List 置 停止）
curl -X PATCH http://127.0.0.1:8000/api/instances/generic_3f9a1b2c \
  -H "Content-Type: application/json" \
  -d '{"status": "停止"}'
```

**预期响应** `200`：返回更新后的 InstanceEntry（service_id 不变）。
**错误**：service_id 不存在 → `404`；四个字段一个都不给 → `400`；`status` 不在 `运行`/`停止`/`异常` 枚举 → `400`。

**数据库验证**：
```bash
# 场景 A：instance 表中该 service_id 的 node 列 + data.address/instance_id 已更新
sqlite3 "$A2X_REGISTRY_DB" \
  "SELECT service_id, node,
          json_extract(data,'\$.address') AS address,
          json_extract(data,'\$.instance_id') AS instance_id
   FROM instance WHERE registry='instances' AND service_id='generic_3f9a1b2c';"
# 预期：generic_3f9a1b2c|192.168.0.20|10.244.3.9:4096|yr-inst-9d1e07

# 场景 B：data.status 已落库
sqlite3 "$A2X_REGISTRY_DB" \
  "SELECT service_id, json_extract(data,'\$.status') AS status
   FROM instance WHERE registry='instances' AND service_id='generic_3f9a1b2c';"
# 预期：generic_3f9a1b2c|停止

# 场景 B 后：默认查询（只回运行）应无该实例；include_unhealthy=true 可见
curl 'http://127.0.0.1:8000/api/instances'
# 预期：[]（generic_3f9a1b2c 已置 停止）
curl 'http://127.0.0.1:8000/api/instances?include_unhealthy=true'
# 预期：含 generic_3f9a1b2c，status="停止"
```

### 2.5 注销实例

**接口**：`DELETE /api/instances/{service_id}`

```bash
curl -X DELETE http://127.0.0.1:8000/api/instances/generic_3f9a1b2c
```

**预期响应** `200`：
```json
{"service_id": "generic_3f9a1b2c", "deleted": true}
```

**效果**：删注册条目（元戎停止由 gateway 完成）；幂等——再删一次返回 `deleted: false`。

**数据库验证**：
```bash
# 1. instance 表该行已删除
sqlite3 "$A2X_REGISTRY_DB" \
  "SELECT COUNT(*) FROM instance
   WHERE registry='instances' AND service_id='generic_3f9a1b2c';"
# 预期：0

# 2. 再次 DELETE 后再查（幂等验证）：依然 0，且响应体 deleted=false
curl -s -X DELETE http://127.0.0.1:8000/api/instances/generic_3f9a1b2c
# 预期响应：{"service_id":"generic_3f9a1b2c","deleted":false}
```

---

## 3. node 心跳 `/api/nodes/{node}/heartbeat`

> **心跳活性不入库**（内存态）：租约存进程内存 `_node_leases`（`NodeHeartbeatStore`）。
> 数据库验证只能间接验证：①心跳续租期间该 node 实例仍在 `instance` 表内；②超 `ttl + grace_period` 未续时 `NodeHeartbeatSweeper` 调 `instance.expire_node` 删除该 node 全部实例（写副作用落库）。
>
> **状态机**：`HEALTHY` --超 ttl--> `UNHEALTHY`（实例派生 `异常`，覆盖落库的 `data.status`）--超 grace_period--> 断连 -> sweeper 剔除该 node 全部实例。
> node 恢复心跳（软恢复）后回到落库的 `data.status`。
> 默认 `ttl=90s`（`min_ttl` 兼作节点租约 TTL）、`grace_period=30s`，可通过 §4 调整。

### 3.1 node 心跳续租（空 body）

**接口**：`POST /api/nodes/{node}/heartbeat`
**场景**：一体机周期性续租，节点级——一次覆盖该 node 全部实例。

```bash
curl -X POST http://127.0.0.1:8000/api/nodes/192.168.0.12/heartbeat \
  -H "Content-Type: application/json" \
  -d '{}'
```

**预期响应** `200`：
```json
{"node": "192.168.0.12", "state": "healthy", "ttl_seconds": 90, "expires_at": 1751800000.0}
```

**效果**：首次心跳装租约（HEALTHY）；续租刷新 ttl。超 `ttl` 未续 -> node 转 UNHEALTHY -> 该 node 实例派生 `异常`；超 `ttl + grace_period` 未续 -> sweeper 调 `expire_node` 删除该 node 全部实例。
**错误**：非 appliance 模式（心跳模块未装配）-> `404`；`lease-config.enabled=false` -> `400`。

**数据库验证**：
```bash
# 心跳本身不入库 —— 验证 instance 表中该 node 实例仍存在（未被剔除）
sqlite3 "$A2X_REGISTRY_DB" \
  "SELECT service_id, node FROM instance
   WHERE registry='instances' AND node='192.168.0.12';"
# 预期（续租期间）：列出该 node 全部实例，例如 generic_3f9a1b2c|192.168.0.12

# 若停止心跳超过 ttl + grace_period（默认 90+30=120s，见 §4）后再查：
#   NodeHeartbeatSweeper 调 instance.expire_node -> 该 node 全部实例行被删除
# 预期（超宽限）：无行返回
```

### 3.2 node 心跳（带状态透传）

```bash
curl -X POST http://127.0.0.1:8000/api/nodes/192.168.0.12/heartbeat \
  -H "Content-Type: application/json" \
  -d '{"status": "loaded"}'
```

**效果**：可选 `status` 字段透传业务状态，不影响租约本身（当前版本仅接收不写库）。

**数据库验证**：
```bash
# 同 §3.1 -- 心跳活性不入库；透传 status 字段不写库
sqlite3 "$A2X_REGISTRY_DB" \
  "SELECT COUNT(*) FROM instance
   WHERE registry='instances' AND node='192.168.0.12';"
# 预期：与 §3.1 一致（仅受 ttl + grace_period / 剔除影响）
```

### 3.3 心跳全生命周期（异常 -> 剔除）

**场景**：验证 node 从 HEALTHY -> UNHEALTHY（异常）-> 超宽限剔除的完整链路。为缩短等待时间，先用 §4 设短 ttl + grace。

```bash
# 0. 前置：已注册实例（见 §2.1），node=192.168.0.12

# 1. 设短租约（ttl=10, grace=5，总等待 15s 即可触发剔除）
curl -X POST http://127.0.0.1:8000/api/lease-config \
  -H "Content-Type: application/json" \
  -d '{"enabled": true, "min_ttl": 10, "max_ttl": 3600, "grace_period": 5}'

# 2. 发一次心跳，装 HEALTHY 租约
curl -X POST http://127.0.0.1:8000/api/nodes/192.168.0.12/heartbeat \
  -H "Content-Type: application/json" -d '{}'

# 3. 等 10s（超过 ttl，未超 grace）-> node 转 UNHEALTHY
sleep 11
# 查实例（include_unhealthy=true 才能看到异常项）
curl 'http://127.0.0.1:8000/api/instances?node=192.168.0.12&include_unhealthy=true'
# 预期：status="异常"（node UNHEALTHY，实例仍存在）

# 默认查询（不含异常）应无返回
curl 'http://127.0.0.1:8000/api/instances?node=192.168.0.12'
# 预期：[]

# 4. 再等 6s（超过 grace_period，总超 15s+）-> sweeper 调 expire_node 剔除
sleep 6
curl 'http://127.0.0.1:8000/api/instances?node=192.168.0.12&include_unhealthy=true'
# 预期：[]（实例已被 sweeper 删除）

# 5. 恢复默认租约
curl -X POST http://127.0.0.1:8000/api/lease-config \
  -H "Content-Type: application/json" \
  -d '{"enabled": true, "min_ttl": 90, "max_ttl": 3600, "grace_period": 30}'
```

**数据库验证**：
```bash
# 步骤 3（超 ttl 未超 grace）：instance 表行仍在（status 不落库，但 API 返回异常）
sqlite3 "$A2X_REGISTRY_DB" \
  "SELECT COUNT(*) FROM instance
   WHERE registry='instances' AND node='192.168.0.12';"
# 预期：1（或该 node 实例数）

# 步骤 4（超 grace）：sweeper 已调 expire_node，行被删除
sqlite3 "$A2X_REGISTRY_DB" \
  "SELECT COUNT(*) FROM instance
   WHERE registry='instances' AND node='192.168.0.12';"
# 预期：0
```

### 3.4 软恢复（宽限内重新心跳）

**场景**：node 超 ttl 转 UNHEALTHY 后，在 grace_period 内重新心跳 -> 恢复 HEALTHY -> 实例回到运行。

```bash
# 0. 前置：设短租约（ttl=10, grace=30），已注册实例 node=192.168.0.12
curl -X POST http://127.0.0.1:8000/api/lease-config \
  -H "Content-Type: application/json" \
  -d '{"enabled": true, "min_ttl": 10, "max_ttl": 3600, "grace_period": 30}'

# 1. 发心跳 -> HEALTHY
curl -X POST http://127.0.0.1:8000/api/nodes/192.168.0.12/heartbeat -d '{}' -H "Content-Type: application/json"

# 2. 等 11s（超 ttl，进入 UNHEALTHY + grace 窗口）
sleep 11
curl 'http://127.0.0.1:8000/api/instances?node=192.168.0.12&include_unhealthy=true'
# 预期：status="异常"

# 3. 宽限内重新心跳 -> 软恢复 HEALTHY
curl -X POST http://127.0.0.1:8000/api/nodes/192.168.0.12/heartbeat -d '{}' -H "Content-Type: application/json"
curl 'http://127.0.0.1:8000/api/instances?node=192.168.0.12'
# 预期：status="运行"（默认查询即可见，因为已恢复 HEALTHY）
```

---

## 4. 心跳租约配置 `/api/lease-config`（全局）

> **存储位置说明**：全局租约策略存进程内存（`NodeHeartbeatStore._config`，`NodeLeaseConfig` 数据类），**不入库**。重启后恢复默认值（`enabled=true, min_ttl=90, max_ttl=3600, grace_period=30`）。
> `min_ttl` 兼作节点租约 TTL（gateway 不发 client TTL，故用 `min_ttl` 作为具体租约时长）；`max_ttl` 为上界（预留，当前未做范围校验）。
> 验证方式：通过 API 读对照，不能用 `sqlite3` 查 `registry_meta`。

### 4.1 读全局租约策略

**接口**：`GET /api/lease-config`

```bash
curl http://127.0.0.1:8000/api/lease-config
```

**预期响应** `200`：
```json
{"enabled": true, "min_ttl": 90, "max_ttl": 3600, "grace_period": 30}
```

**验证**（配置不入库，通过 API 对照）：
```bash
# 再读一次 API 确认一致性
curl -s http://127.0.0.1:8000/api/lease-config | python3 -m json.tool
# 预期：{"enabled": true, "min_ttl": 90, "max_ttl": 3600, "grace_period": 30}
```

### 4.2 改全局租约策略

**接口**：`POST /api/lease-config`

```bash
curl -X POST http://127.0.0.1:8000/api/lease-config \
  -H "Content-Type: application/json" \
  -d '{"enabled": true, "min_ttl": 90, "max_ttl": 3600, "grace_period": 60}'
```

**效果**：调整 `grace_period` 后，后续超宽限时间随之变化；已发的租约按新策略续期。配置仅存内存，重启后恢复默认值。

**验证**（配置不入库，通过 API 对照）：
```bash
# 再读 API 对照（端到端一致性）
curl -s http://127.0.0.1:8000/api/lease-config | python3 -m json.tool
# 预期响应：{"enabled": true, "min_ttl": 90, "max_ttl": 3600, "grace_period": 60}
```

---

## 5. 分布式高可用 `/api/ha/*`（后续版本，730 不实现）

> 730 单机 SQLite 不实现 HA；接口契约已在 OpenAPI 标注 `[后续版本]`，与开发计划 P1-2「`ha/` 模块整体不建」一致。下列命令仅供后续版本联调参考。

### 5.1 查询当前成员集

```bash
curl http://127.0.0.1:8000/api/ha/members
```

**预期响应** `200`：
```json
{"members": ["192.168.0.11", "192.168.0.12", "192.168.0.13"]}
```

### 5.2 变更成员集（奇偶校验）

```bash
curl -X POST http://127.0.0.1:8000/api/ha/members \
  -H "Content-Type: application/json" \
  -d '{"members": ["192.168.0.11", "192.168.0.12"]}'
```

**预期响应** `200`：
```json
{"members": ["192.168.0.11"], "warning": "偶数台，未激活 192.168.0.12"}
```

**效果**：偶数成员集自动去尾 + 告警，保证 Raft 多数派为奇数。

### 5.3 查询当前主（leader）

```bash
curl http://127.0.0.1:8000/api/ha/leader
```

**预期响应** `200`：
```json
{"leader": "192.168.0.11"}
```

**效果**：任一节点据 Raft 权威回主；注册中心据此把 nginx 指向主，gateway 不直接调用。

> **数据库验证**：当前不实现 HA，无对应业务表，跳过数据库验证。

---

## 6. 典型联调场景

### 场景 A：gateway 拉起一个新实例（端到端）

```bash
# 1. 取运行规格
curl http://127.0.0.1:8000/api/images/opencode/launch-spec

# 2. gateway 调元戎拉起（注册中心不参与），得到 address=10.244.1.7:4096

# 3. 注册实例
curl -X POST http://127.0.0.1:8000/api/instances \
  -H "Content-Type: application/json" \
  -d '{
    "service_id": "generic_3f9a1b2c",
    "kind": "三方", "framework": "opencode", "framework_version": "v0.2.0",
    "node": "192.168.0.12", "address": "10.244.1.7:4096", "user": "user-01"
  }'

# 4. 周期性 node 心跳（period = ttl/3）
curl -X POST http://127.0.0.1:8000/api/nodes/192.168.0.12/heartbeat \
  -H "Content-Type: application/json" -d '{}'
```

**数据库验证（场景 A）**：
```bash
# 端到端落库校验：实例入库 + 心跳未触发剔除
sqlite3 "$A2X_REGISTRY_DB" \
  "SELECT service_id, kind, framework, node,
          json_extract(data,'\$.address') AS address
   FROM instance
   WHERE registry='instances' AND service_id='generic_3f9a1b2c';"
# 预期：generic_3f9a1b2c|三方|opencode|192.168.0.12|10.244.1.7:4096
```

### 场景 B：实例落点迁移

```bash
# 1. gateway 检测到元戎迁移、新 address
# 2. 更新实例条目（service_id 不变）
curl -X PATCH http://127.0.0.1:8000/api/instances/generic_3f9a1b2c \
  -H "Content-Type: application/json" \
  -d '{"node": "192.168.0.20", "address": "10.244.3.9:4096"}'

# 3. 新 node 立即心跳，避免被误判异常
curl -X POST http://127.0.0.1:8000/api/nodes/192.168.0.20/heartbeat \
  -H "Content-Type: application/json" -d '{}'
```

**数据库验证（场景 B）**：
```bash
# 1. 实例已迁到新 node，address 已更新；service_id 不变
sqlite3 "$A2X_REGISTRY_DB" \
  "SELECT service_id, node, json_extract(data,'\$.address') AS address
   FROM instance WHERE registry='instances' AND service_id='generic_3f9a1b2c';"
# 预期：generic_3f9a1b2c|192.168.0.20|10.244.3.9:4096

# 2. 旧 node='192.168.0.12' 下已无该实例
sqlite3 "$A2X_REGISTRY_DB" \
  "SELECT COUNT(*) FROM instance
   WHERE registry='instances' AND service_id='generic_3f9a1b2c' AND node='192.168.0.12';"
# 预期：0
```

### 场景 C：镜像版本下线

```bash
# 1. 确认无在用实例
curl 'http://127.0.0.1:8000/api/instances?framework=opencode&framework_version=v0.2.0'

# 2. 注销镜像（返回 409 image_in_use 则先迁走实例）
curl -X DELETE http://127.0.0.1:8000/api/images/opencode/v0.2.0
```

**数据库验证（场景 C）**：
```bash
# 1. image 表对应版本行已删除
sqlite3 "$A2X_REGISTRY_DB" \
  "SELECT COUNT(*) FROM image
   WHERE registry='images' AND framework='opencode' AND framework_version='v0.2.0';"
# 预期：0

# 2. 该 framework 下若仍有其他版本，应恰有一行 is_default=1（自动补默认）
sqlite3 "$A2X_REGISTRY_DB" \
  "SELECT framework_version, is_default
   FROM image WHERE registry='images' AND framework='opencode'
   ORDER BY is_default DESC, framework_version;"
# 预期：剩余版本中恰一行 is_default=1；若该 framework 已无版本则空

# 3. 注销前的在用实例校验：instance 表中引用该 fw+ver 的行（DELETE 镜像前应已迁走）
sqlite3 "$A2X_REGISTRY_DB" \
  "SELECT COUNT(*) FROM instance
   WHERE registry='instances' AND framework='opencode' AND framework_version='v0.2.0';"
# 预期（成功下线后）：0（镜像可删的前提就是无在用实例）
```

### 场景 D：node 故障 -> 实例自动异常 -> 超宽限剔除

```bash
# 1. 设短租约加速验证（ttl=10, grace=5），然后发一次心跳
curl -X POST http://127.0.0.1:8000/api/lease-config \
  -H "Content-Type: application/json" \
  -d '{"enabled": true, "min_ttl": 10, "max_ttl": 3600, "grace_period": 5}'
curl -X POST http://127.0.0.1:8000/api/nodes/192.168.0.12/heartbeat \
  -H "Content-Type: application/json" -d '{}'

# 2. 等 11s（超 ttl，进入 UNHEALTHY + grace）-> 查实例（含异常）
sleep 11
curl 'http://127.0.0.1:8000/api/instances?node=192.168.0.12&include_unhealthy=true'
# 预期：status="异常"（实例仍存在）

# 3. 再等 6s（超 grace_period）-> NodeHeartbeatSweeper 自动调 expire_node 剔除，无需手工调用
sleep 6
curl 'http://127.0.0.1:8000/api/instances?node=192.168.0.12&include_unhealthy=true'
# 预期：[]

# 4. 恢复默认租约
curl -X POST http://127.0.0.1:8000/api/lease-config \
  -H "Content-Type: application/json" \
  -d '{"enabled": true, "min_ttl": 90, "max_ttl": 3600, "grace_period": 30}'
```

**数据库验证（场景 D）**：
```bash
# 1. grace_period 策略不入库，通过 API 对照确认（配置仅存内存）
curl -s http://127.0.0.1:8000/api/lease-config
# 预期（步骤 1 后）：{"enabled":true,"min_ttl":10,"max_ttl":3600,"grace_period":5}

# 2. 等 ttl + grace_period 后查 instance 表：NodeHeartbeatSweeper 调 expire_node 应已删除该 node 全部实例
sqlite3 "$A2X_REGISTRY_DB" \
  "SELECT COUNT(*) FROM instance
   WHERE registry='instances' AND node='192.168.0.12';"
# 预期（超宽限后）：0

# 3. 其他 node 上的实例不受影响（横向校验）
sqlite3 "$A2X_REGISTRY_DB" \
  "SELECT DISTINCT node FROM instance WHERE registry='instances';"
# 预期：故障 node 不在列表中；其他 node 仍列出
```

### 场景 E：注册中心重启 -> node 心跳重建

**场景**：注册中心重启后，实例数据在 SQLite 中不丢；心跳活性（内存态）清空。启动时 `recover_from_persisted(distinct_nodes)` 从 `instance` 表取 distinct node，为每个 node 装 UNHEALTHY + grace 租约。gateway 重新心跳则恢复 HEALTHY；不心跳则超 grace 后剔除。

```bash
# 0. 前置：node 192.168.0.12 上有注册实例，设短租约加速验证
curl -X POST http://127.0.0.1:8000/api/lease-config \
  -H "Content-Type: application/json" \
  -d '{"enabled": true, "min_ttl": 90, "max_ttl": 3600, "grace_period": 10}'
curl -X POST http://127.0.0.1:8000/api/nodes/192.168.0.12/heartbeat \
  -H "Content-Type: application/json" -d '{}'

# 1. 重启注册中心（Ctrl+C 停止后重新启动）
#    ./build_test/a2x-registry

# 2. 重启后立即查实例 -- 实例仍在库中，但 node 租约已重建为 UNHEALTHY
#    status 应为 "异常"（recover_from_persisted 装的是 UNHEALTHY + grace 租约）
curl 'http://127.0.0.1:8000/api/instances?node=192.168.0.12&include_unhealthy=true'
# 预期：status="异常"（node 处于宽限期 UNHEALTHY，等 gateway 重新心跳）

# 3. gateway 重新心跳 -> 软恢复 HEALTHY
curl -X POST http://127.0.0.1:8000/api/nodes/192.168.0.12/heartbeat \
  -H "Content-Type: application/json" -d '{}'
curl 'http://127.0.0.1:8000/api/instances?node=192.168.0.12'
# 预期：status="运行"（已恢复 HEALTHY，默认查询即可见）

# 4. 若重启后 gateway 未在 grace_period 内重新心跳（此处 grace=10s）
#    等 11s 后 -> sweeper 调 expire_node 删除该 node 全部实例
sleep 11
curl 'http://127.0.0.1:8000/api/instances?node=192.168.0.12&include_unhealthy=true'
# 预期：[]（实例已被 sweeper 删除）

# 5. 恢复默认租约
curl -X POST http://127.0.0.1:8000/api/lease-config \
  -H "Content-Type: application/json" \
  -d '{"enabled": true, "min_ttl": 90, "max_ttl": 3600, "grace_period": 30}'
```

**数据库验证（场景 E）**：
```bash
# 1. 重启后实例仍在库中（SQLite 持久化，不受重启影响）
sqlite3 "$A2X_REGISTRY_DB" \
  "SELECT service_id, node FROM instance
   WHERE registry='instances' AND node='192.168.0.12';"
# 预期（步骤 2）：列出该 node 全部实例

# 2. 若 gateway 重新心跳（步骤 3）-> 实例仍在，status 恢复运行
sqlite3 "$A2X_REGISTRY_DB" \
  "SELECT COUNT(*) FROM instance
   WHERE registry='instances' AND node='192.168.0.12';"
# 预期（步骤 3 后）：1（或该 node 实例数，未变）

# 3. 若 gateway 未在 grace 内重新心跳（步骤 4）-> sweeper 剔除，行被删除
sqlite3 "$A2X_REGISTRY_DB" \
  "SELECT COUNT(*) FROM instance
   WHERE registry='instances' AND node='192.168.0.12';"
# 预期（步骤 4 后）：0
```

---

## 7. 错误码速查

| HTTP | 场景 | 响应体 |
|------|------|--------|
| `400` | 注册镜像 spec.rootfs.imageurl 缺失 / filter key 不在白名单 / node 心跳 `enabled=false` | `{"detail":"..."}` |
| `404` | 取不存在的 framework launch-spec / PATCH 不存在的 service_id / 非 appliance 模式调 node 心跳或 lease-config | `{"detail":"..."}` |
| `409` | 注销在用镜像 | `{"code":"image_in_use","detail":"...","instances":[...]}` |
| `502` | 注销镜像时镜像仓删除接口失败（外部依赖） | `{"detail":"..."}` |

> `401` / `403` 鉴权错误不在 730 范围（不启鉴权），后续版本启用 `auth/` 模块后补充。
