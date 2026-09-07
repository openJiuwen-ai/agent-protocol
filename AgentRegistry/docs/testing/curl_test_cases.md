# 注册中心 API · 手工测试 curl 命令合集

> 基于 registry_openapi.yaml，覆盖一体机场景全部接口。
> 默认服务器：`http://127.0.0.1:8000`。
> 命令可直接复制到 bash 运行；每个接口选 1-2 个典型参数组合。

## 约定

- 成功响应只列关键字段，完整形状见 OpenAPI schema。
- 错误路径在每个接口末尾标注典型一种。
- 联调场景见末尾 §4。
- **数据库验证（SQLite）**：每个 curl 步骤后给出 `sqlite3` 命令，直接查 `$A2X_REGISTRY_DB` 验证落库效果。环境变量约定：
  ```bash
  # 730 单机 SQLite，路径由 registry.env 的 A2X_REGISTRY_HOME 决定
  export A2X_REGISTRY_DB="${A2X_REGISTRY_HOME:-/var/lib/a2x-registry}/registry.db"
  ```
- 表结构真源：[a2x_registry/common/db.py](../../a2x_registry/common/db.py) 的 `SCHEMA_SQL`（4 表：`registry_meta` / `service` / `image` / `instance`）。
- **镜像注册表以 `name` 为主键**：`name` 取代原 `framework` 的定位作用（主键 / 默认版本 / 检索）；`framework` 降级为纯展示字段（仍可按其筛选）；`framework_version` 更名 `version`（过渡期 `framework_version` 兼容回退、标记待删除）；新增 `description` / `package_path` / `image_archive_path` 纯文本字段与 `access_mode` 接入方式数组（每项 `{name, port, cmd}`）。接口路径 `{framework}` 全部改 `{name}`。
- **实例 status 落库（data JSON）**：`instance` 表无 `status` 列，status 存在行内 `data` JSON（`data.status`，注册默认 `运行`，gateway 据元戎 List 经 PATCH 写入 `停止`/`异常`/`运行`）。
- **注册中心不收心跳**：节点心跳 `/api/nodes/{node}/heartbeat` 与全局 `/api/lease-config` 已移除；实例存活由 gateway 轮询元戎 List、经 `PATCH` 写 `status`，注册中心不派生、不自动剔除。

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
    "name": "opencode",
    "framework": "opencode",
    "description": "opencode 适配镜像",
    "package_path": "/pkg/opencode/",
    "image_archive_path": "/archive/opencode.tar",
    "version": "v0.2.0",
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
    "access_mode": [
      {"name": "tui", "port": "2222", "cmd": "opencode"},
      {"name": "web", "port": "18789", "cmd": "opencode gateway --port 18789"}
    ],
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

**效果**：按 `name + version` 幂等 upsert；该 name 首次注册时自动置为默认版本。`runtime_spec` 为不透明 JSON 透传，注册中心不解析其内部字段。`framework` 为纯展示字段（非主键）；`description` / `package_path` / `image_archive_path` / `access_mode` 为 §6 新增字段，落 `data` JSON。
**错误**：`runtime_spec` 缺失 -> `422`（pydantic 校验错误）；`version` 与过渡期 `framework_version` 均缺失 -> `400`。

**数据库验证**：
```bash
# 1. image 表新增一行（registry='images'），name/framework/version 为提升列，
#    data JSON 含 runtime_spec 透传 + §6 新字段
sqlite3 "$A2X_REGISTRY_DB" \
  "SELECT name, framework, version, is_default,
          json_extract(data,'$.runtime_spec.rootfs.imageurl') AS imageurl,
          json_extract(data,'$.runtime_spec.cpu') AS cpu,
          json_extract(data,'$.description') AS description,
          json_extract(data,'$.package_path') AS package_path,
          json_extract(data,'$.image_archive_path') AS archive_path
   FROM image WHERE registry='images' AND name='opencode';"
# 预期：opencode|opencode|v0.2.0|1|harbor.local/adapted/opencode:v0.2.0-mod1.3|1000|opencode 适配镜像|/pkg/opencode/|/archive/opencode.tar

# 2. registry_meta 已登记 'images'（启动期 create_registry）
sqlite3 "$A2X_REGISTRY_DB" \
  "SELECT registry, kind FROM registry_meta WHERE registry='images';"
# 预期：images|image
```

> **过渡期兼容**：请求体只带 `framework_version`（不带 `version`）也能注册——`version` 缺省时回退 deprecated `framework_version`。

### 1.2 查询镜像（按 name 过滤）

**接口**：`GET /api/images?name={name}`

```bash
# 按主键 name 过滤
curl 'http://127.0.0.1:8000/api/images?name=opencode'

# 按 framework 展示字段过滤（降级为普通筛选）
curl 'http://127.0.0.1:8000/api/images?framework=opencode'
```

**预期响应** `200`：
```json
[{
  "name": "opencode",
  "framework": "opencode",
  "description": "opencode 适配镜像",
  "package_path": "/pkg/opencode/",
  "image_archive_path": "/archive/opencode.tar",
  "version": "v0.2.0",
  "is_default": true,
  "access_mode": [{"name": "tui", "port": "2222", "cmd": "opencode"}],
  "image_module_version": "v1.3",
  "runtime_spec": {"runtime": "python3.11", "rootfs": {"imageurl": "..."}, "cpu": 1000, ...},
  "workspace": "/app",
  "mounts": [{"source": "/data/agent", "target": "/data"}],
  "env_vars": {"A2X_LLM_KEY": "${A2X_LLM_KEY}"},
  "uploaded_by": "user-01",
  "created_at": "2026-07-06T10:00:00Z"
}]
```

**效果**：扁平数组返回（一条目 = 一个 name 的一个版本），`runtime_spec` 为不透明透传。排序 `name asc, version_key desc`（同 name 新版本在前）。不传参数返回全部；`?name=` 按主键过滤，`?framework=` 按展示字段过滤。支持 `?size` / `?page` 分页。

**数据库验证**：
```bash
# 对照 image 表中该 name 全部版本行 + 默认版本指针
sqlite3 "$A2X_REGISTRY_DB" \
  "SELECT name, version, is_default,
          json_extract(data,'$.image_module_version') AS mod_ver
   FROM image WHERE registry='images' AND name='opencode'
   ORDER BY is_default DESC, version;"
# 预期首行：opencode|v0.2.0|1|v1.3（is_default=1 排在前）
```

### 1.3 取运行规格（gateway 拉起前调用）

**接口**：`GET /api/images/{name}/launch-spec?version={ver}`

```bash
# 默认版本
curl http://127.0.0.1:8000/api/images/opencode/launch-spec

# 指定版本
curl 'http://127.0.0.1:8000/api/images/opencode/launch-spec?version=v0.2.0'
```

**预期响应** `200`：
```json
{
  "name": "opencode",
  "framework": "opencode",
  "version": "v0.2.0",
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
  "access_mode": [
    {"name": "tui", "port": "2222", "cmd": "opencode"},
    {"name": "web", "port": "18789", "cmd": "opencode gateway --port 18789"}
  ],
  "env_vars": {"A2X_LLM_KEY": "${A2X_LLM_KEY}"},
  "workspace": "/app",
  "mounts": [{"source": "/data/agent", "target": "/data"}],
  "image_module_version": "v1.3"
}
```

**效果**：返回元戎运行规格（`runtime_spec` 不透明透传 + `access_mode`/`env_vars`/`workspace`/`mounts` 顶层字段，gateway 据 `access_mode` 选端口与启动命令）；不带 version 取默认版本。
**错误**：name 或版本不存在 -> `404 {"detail":"..."}`。

**数据库验证**：
```bash
# 默认版本路径：查 is_default=1 那一行的 data
sqlite3 "$A2X_REGISTRY_DB" \
  "SELECT version,
          json_extract(data,'$.runtime_spec.rootfs.imageurl') AS imageurl,
          json_extract(data,'$.runtime_spec.cpu') AS cpu,
          json_extract(data,'$.access_mode') AS access_mode
   FROM image
   WHERE registry='images' AND name='opencode' AND is_default=1;"
# 预期：v0.2.0|harbor.local/adapted/opencode:v0.2.0-mod1.3|1000|[{"name":"tui","port":"2222","cmd":"opencode"},...]
```

### 1.4 设默认版本

**接口**：`PUT /api/images/{name}/default`

```bash
# 假设此前已注册 opencode 的 v0.1.0 与 v0.2.0 两个版本
curl -X PUT http://127.0.0.1:8000/api/images/opencode/default \
  -H "Content-Type: application/json" \
  -d '{"version": "v0.2.0"}'
```

**预期响应** `200`：
```json
{"name": "opencode", "framework": "opencode", "default": "v0.2.0", "status": "updated"}
```

**效果**：清该 name 旧 `is_default`、置新版为 1。
**错误**：name 不存在 -> `404`；`version` 与过渡期 `framework_version` 均缺失 -> `400`。

**数据库验证**：
```bash
# 同 name 下应恰有一行 is_default=1（新版），其他均为 0
sqlite3 "$A2X_REGISTRY_DB" \
  "SELECT version, is_default
   FROM image WHERE registry='images' AND name='opencode'
   ORDER BY version;"
# 预期（两版本场景）：v0.1.0|0  /  v0.2.0|1
```

### 1.5 注销镜像

**接口**：`DELETE /api/images/{name}/{version}`

```bash
curl -X DELETE http://127.0.0.1:8000/api/images/opencode/v0.2.0
```

**预期响应** `200`：
```json
{"name": "opencode", "framework": "opencode", "version": "v0.2.0", "status": "deregistered"}
```

**效果**：先校验无在用实例 -> 删镜像仓文件 -> 删条目（删的是默认版本则把最新版补为默认）。
**错误**：仍有在用实例 -> `409 {"detail":"image opencode@v0.2.0 still has N in-use instance(s); cannot deregister"}`。

**数据库验证**：
```bash
# 1. image 表对应行已删除
sqlite3 "$A2X_REGISTRY_DB" \
  "SELECT COUNT(*) FROM image
   WHERE registry='images' AND name='opencode' AND version='v0.2.0';"
# 预期：0

# 2. 若删的是默认版本，应自动补一个新默认（同 name 下剩余行恰一行 is_default=1）
sqlite3 "$A2X_REGISTRY_DB" \
  "SELECT COUNT(*) FROM image
   WHERE registry='images' AND name='opencode' AND is_default=1;"
# 预期：1（如还有其他版本）或 0（如该 name 已无任何版本）
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

**效果**：`status` 为落库字段（`data.status`：运行 / 停止 / 异常，gateway 据元戎 List 经 PATCH 写入，注册中心不派生）；`include_unhealthy=false` 默认只回 `运行`（`停止`/`异常` 均被过滤，SQL 下推 `COALESCE(json_extract(data,'$.status'),'运行')='运行'`）。
**支持的 filter key**：`include_unhealthy` / `node` / `framework` / `kind` / `user`（白名单，其他 key 返回 `400`）。

**数据库验证**：
```bash
# 对照 instance 表中该 node 上的全部实例（status 落库在 data JSON）
sqlite3 "$A2X_REGISTRY_DB" \
  "SELECT service_id, kind, framework, node, \"user\",
          json_extract(data,'\$.address') AS address,
          json_extract(data,'\$.status') AS status
   FROM instance WHERE registry='instances' AND node='192.168.0.12';"
# 预期：列出该 node 全部实例（含 data.status='停止'/'异常' 但默认查询被过滤的行）

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

## 3. 节点心跳（已移除）

注册中心**不再接收节点心跳**：`POST /api/nodes/{node}/heartbeat` 与 `GET/POST /api/lease-config` 已移除（调用返回 `404`）。

实例存活由 gateway 周期轮询**元戎 List** 掌握：不在运行的实例经 `PATCH /api/instances/{service_id}` 置 `停止` / `异常`（见 §2.4 场景 B）；注册中心不派生状态、不自动剔除。

---

## 4. 分布式高可用 `/api/ha/*`（后续版本，730 不实现）

> 730 单机 SQLite 不实现 HA；接口契约已在 OpenAPI 标注 `[后续版本]`，与开发计划 P1-2「`ha/` 模块整体不建」一致。下列命令仅供后续版本联调参考。

### 4.1 查询当前成员集

```bash
curl http://127.0.0.1:8000/api/ha/members
```

**预期响应** `200`：
```json
{"members": ["192.168.0.11", "192.168.0.12", "192.168.0.13"]}
```

### 4.2 变更成员集（奇偶校验）

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

### 4.3 查询当前主（leader）

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

## 5. 典型联调场景

### 场景 A：gateway 拉起一个新实例（端到端）

```bash
# 1. 取运行规格
curl http://127.0.0.1:8000/api/images/opencode/launch-spec

# 2. gateway 调元戎拉起（注册中心不参与），得到 address=10.244.1.7:4096

# 3. 注册实例（含元戎返回的 instance_id）
curl -X POST http://127.0.0.1:8000/api/instances \
  -H "Content-Type: application/json" \
  -d '{
    "service_id": "generic_3f9a1b2c",
    "kind": "三方", "framework": "opencode", "framework_version": "v0.2.0",
    "node": "192.168.0.12", "instance_id": "yr-inst-7f3a92",
    "address": "10.244.1.7:4096", "user": "user-01"
  }'

# 4. gateway 周期轮询元戎 List 维持 status（注册中心不收心跳）：
#    发现实例不在运行时经 PATCH 置 停止/异常（见 §2.4 场景 B）
```

**数据库验证（场景 A）**：
```bash
# 端到端落库校验：实例入库（status 默认 运行）
sqlite3 "$A2X_REGISTRY_DB" \
  "SELECT service_id, kind, framework, node,
          json_extract(data,'\$.address') AS address,
          json_extract(data,'\$.instance_id') AS instance_id,
          json_extract(data,'\$.status') AS status
   FROM instance
   WHERE registry='instances' AND service_id='generic_3f9a1b2c';"
# 预期：generic_3f9a1b2c|三方|opencode|192.168.0.12|10.244.1.7:4096|yr-inst-7f3a92|运行
```

### 场景 B：实例落点迁移

```bash
# 1. gateway 检测到元戎迁移、新 address + 新 instance_id
# 2. 更新实例条目（service_id 不变）
curl -X PATCH http://127.0.0.1:8000/api/instances/generic_3f9a1b2c \
  -H "Content-Type: application/json" \
  -d '{"node": "192.168.0.20", "address": "10.244.3.9:4096", "instance_id": "yr-inst-9d1e07"}'

# 3. gateway 后续轮询元戎 List 发现实例仍运行 -> status 保持 运行
#    （注册中心不收心跳、不自动剔除，无需任何额外调用）
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
   WHERE registry='images' AND name='opencode' AND version='v0.2.0';"
# 预期：0

# 2. 该 name 下若仍有其他版本，应恰有一行 is_default=1（自动补默认）
sqlite3 "$A2X_REGISTRY_DB" \
  "SELECT version, is_default
   FROM image WHERE registry='images' AND name='opencode'
   ORDER BY is_default DESC, version;"
# 预期：剩余版本中恰一行 is_default=1；若该 name 已无版本则空

# 3. 注销前的在用实例校验：instance 表中引用该 fw+ver 的行（DELETE 镜像前应已迁走）
sqlite3 "$A2X_REGISTRY_DB" \
  "SELECT COUNT(*) FROM instance
   WHERE registry='instances' AND framework='opencode' AND framework_version='v0.2.0';"
# 预期（成功下线后）：0（镜像可删的前提就是无在用实例）
```

### 场景 D：实例停止（gateway 据元戎 List 置 停止/异常）

> 注册中心不收心跳、不派生状态、不自动剔除。gateway 轮询元戎
> List 发现实例不在运行时，经 PATCH 写 `status`；`instance` 行**不会**被自动删除。

```bash
# 1. 前置：generic_3f9a1b2c 已注册且 status=运行（见场景 A）

# 2. gateway 轮询元戎 List 发现实例已停 -> PATCH 置 停止
curl -X PATCH http://127.0.0.1:8000/api/instances/generic_3f9a1b2c \
  -H "Content-Type: application/json" \
  -d '{"status": "停止"}'

# 3. 默认查询（只回运行）应无该实例
curl 'http://127.0.0.1:8000/api/instances'
# 预期：[]

# 4. 运维诊断用 include_unhealthy=true -> 可见，status="停止"，条目仍在
curl 'http://127.0.0.1:8000/api/instances?include_unhealthy=true'
# 预期：含 generic_3f9a1b2c，status="停止"

# 5. 元戎侧实例恢复（或 gateway 重新拉起并注册同 service_id）-> PATCH 置回 运行
curl -X PATCH http://127.0.0.1:8000/api/instances/generic_3f9a1b2c \
  -H "Content-Type: application/json" \
  -d '{"status": "运行"}'
curl 'http://127.0.0.1:8000/api/instances'
# 预期：generic_3f9a1b2c 再次出现在默认列表
```

**数据库验证（场景 D）**：
```bash
# status 落库在 data JSON；条目不会被自动删除
sqlite3 "$A2X_REGISTRY_DB" \
  "SELECT service_id, json_extract(data,'\$.status') AS status
   FROM instance WHERE registry='instances' AND service_id='generic_3f9a1b2c';"
# 预期（步骤 2 后）：generic_3f9a1b2c|停止
# 预期（步骤 5 后）：generic_3f9a1b2c|运行
```

### 场景 E：注册中心重启 -> 实例数据不丢

**场景**：注册中心重启后，实例数据在 SQLite 中不丢；`status` 为落库字段，重启后保持重启前的值（无内存态需要恢复——心跳活性已不存在）。

```bash
# 0. 前置：generic_3f9a1b2c 已注册，status=运行（见场景 A）

# 1. 重启注册中心（Ctrl+C 停止后重新启动）
#    ./build_test/a2x-registry

# 2. 重启后查询：实例仍在，status 保持落库值（运行）
curl 'http://127.0.0.1:8000/api/instances?node=192.168.0.12'
# 预期：含 generic_3f9a1b2c，status="运行"
```

**数据库验证（场景 E）**：
```bash
# 重启后实例仍在库中（SQLite 持久化，不受重启影响）
sqlite3 "$A2X_REGISTRY_DB" \
  "SELECT service_id, node, json_extract(data,'\$.status') AS status
   FROM instance
   WHERE registry='instances' AND node='192.168.0.12';"
# 预期：列出该 node 全部实例，status 与重启前一致
```

---

## 6. 错误码速查

| HTTP | 场景 | 响应体 |
|------|------|--------|
| `400` | 注册镜像 spec.rootfs.imageurl 缺失 / filter key 不在白名单 / PATCH status 不在 运行/停止/异常 枚举 | `{"detail":"..."}` |
| `404` | 取不存在的 name launch-spec / PATCH 不存在的 service_id / 调已移除的节点心跳 `/api/nodes/{node}/heartbeat` 或 `/api/lease-config` | `{"detail":"..."}` |
| `409` | 注销在用镜像 | `{"code":"image_in_use","detail":"...","instances":[...]}` |
| `502` | 注销镜像时镜像仓删除接口失败（外部依赖） | `{"detail":"..."}` |

> `401` / `403` 鉴权错误不在 730 范围（不启鉴权），后续版本启用 `auth/` 模块后补充。
> 注：`/api/nodes/{node}/heartbeat` 与 `/api/lease-config` 已移除，调用返回 `404`。
