## -1. 前置依赖
部署需要一台物理机，包含以下组件：
- bash
- Docker
- Docker Compose
- Python
- curl

## 0. 组网架构
当前组网架构为3节点，通过docker compose拉起，每个节点1个rqlite容器。
测试注册中心时，需要启动三个注册中心实例，模拟3节点集群，分别连接到宿主的4001、4011、4021端口。

### 端口规划（已在脚本中配好）

| 节点 | 容器名 | 宿主 HTTP | 宿主 Raft | 容器内 |
|------|--------|----------|----------|--------|
| node-1 | myrqlite-container-1 | 4001 | 4002 | 4001/4002 |
| node-2 | myrqlite-container-2 | 4011 | 4012 | 4001/4002 |
| node-3 | myrqlite-container-3 | 4021 | 4022 | 4001/4002 |

### 组网拓扑
```
                 ┌─────────────── 宿主命名空间 ───────────────────┐
                 │                                              │
外网 ─ （物理网卡如eth0） ─ default ─ docker NAT                   │
                 │              │                               │
                 │     5: br-xxxxxx网桥 172.18.0.1/16            │
                 │       │         │         │                  │
                 │       6         7         8  (veth 宿主端)    │
                 └───────┼─────────┼─────────┼──────────────────┘
                         │         │         │  (netns 边界)
                ┌────────▼───┐ ┌───▼────┐ ┌──▼───────┐
                │ container-1│ │ cont-2 │ │ cont-3   │
                │ eth0       │ │ eth0   │ │ eth0     │
                │ 172.18.0.2 │ │.0.4    │ │.0.3      │
                │ :4001 HTTP │ │:4001   │ │:4001     │
                │ :4002 Raft │ │:4002   │ │:4002     │
                └────────────┘ └────────┘ └──────────┘
                HTTP端口，分别映射到宿主的4001、4011、4021
                Raft端口，分别映射到宿主的4002、4012、4022
```
- 5号：Docker 为 compose 项目自动创建的网桥，对容器间流量是二层交换机角色，对出网流量是三层网关角色（路由 + NAT），具体的路由表可以进入容器里查看。
- 6、7、8 号：每个节点的 eth0 对应一个 veth 对，veth 对的另一端挂在网桥上，容器内 eth0 对应 172.18.0.2、.4、.3。容器间通过 eth0 通信，宿主机通过 172.18.0.1 访问容器。


## 1. 启动集群

```bash
# cd 到start_rqlite_cluster.sh脚本所在目录
chmod +x start_rqlite_cluster.sh
./start_rqlite_cluster.sh up
```
脚本执行后会生成 compose.yaml 并启动集群。

如果没有公网连接，可以手动将rqlite镜像tar包复制到脚本所在目录，然后通过docker加载：
```bash
./start_rqlite_cluster --image-tar rqlite.tar up
```

集群启动后，如果有浏览器，也可以访问节点的 HTTP 端口查看集群状态和执行SQL语句：
- node-1：[http://localhost:4001](http://localhost:4001)
- node-2：[http://localhost:4011](http://localhost:4011)
- node-3：[http://localhost:4021](http://localhost:4021)

访问以上任一地址即可，rqlite支持自动转发请求到当前leader节点。

## 2. 查看容器状态

```bash
./start_rqlite_cluster status
```

## 3. 查询集群状态（任一节点都可查）

```bash
# 查 leader
curl -s http://localhost:4001/status | python3 -m json.tool | grep -E "leader|addr"

# 查节点列表
curl -s http://localhost:4001/nodes | python3 -m json.tool
```
三个节点应都返回 ` Voter ` 状态。

## 4. 写入测试（连任意节点写，leader 处理）

```bash
curl -s -X POST http://localhost:4001/db/execute \
  -H 'Content-Type: application/json' \
  -d '[["CREATE TABLE IF NOT EXISTS foo (id INTEGER, name TEXT)"]]'

curl -s -X POST http://localhost:4001/db/execute \
  -H 'Content-Type: application/json' \
  -d '[["INSERT INTO foo (id, name) VALUES (1, "hello-rqlite")"]]'
```

## 5. 读验证（连**另外两个节点**读，验证 Raft 复制）

```bash
# node-2（宿主 4011）
curl -s "http://localhost:4011/db/query?q=SELECT * FROM foo" | python3 -m json.tool

# node-3（宿主 4021）
curl -s "http://localhost:4021/db/query?q=SELECT * FROM foo" | python3 -m json.tool
```
三个节点都应返回 `{"results":[{"values":[[1,"hello-rqlite"]]}]}`。

## 6. 故障切换测试（可选）

```bash
# 停 leader（假设是 node-2）
docker compose stop myrqlite-service-2

# 等几秒，重查状态——新 leader 应已选出
curl -s http://localhost:4001/status | python3 -m json.tool | grep leader

# 写入应仍成功（验证 HA）
curl -s -X POST http://localhost:4001/db/execute \
  -H 'Content-Type: application/json' \
  -d '[["INSERT INTO foo (id, name) VALUES (2, "after-failover")"]]'

# 恢复节点
docker compose start myrqlite-service-2
```

## 7. 清理（保留卷以便复用）

```bash
./start_rqlite_cluster.sh down
```
彻底清数据：
```bash
./start_rqlite_cluster.sh clean
```

## 备注：选主不确定性

README 示例日志里是 **node-2 成为 leader**，但 rqlite 首次启动选主有随机性，实际可能是 node-1 或 node-3。`grep Leader` 确认即可，不必跟 README 完全一致。

后续与注册中心集成时，只需在registry.env里设置：
```env
A2X_REGISTRY_DB_KIND=rqlite
A2X_REGISTRY_DB_ENDPOINT=http://127.0.0.1:4001   # 或 4011/4021
```
即可对接这个集群。