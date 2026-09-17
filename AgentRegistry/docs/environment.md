# 环境变量总表

A2X Registry 读取的全部环境变量。**所有变量都在进程启动时读取，运行时修改不生效**——改完需要重启 `a2x-registry`。

> 本项目不使用 dotenv，没有 `.env` 加载器：变量需要通过 shell `export`、systemd 的 `Environment=` / `EnvironmentFile=`，或容器编排注入。

## 1. 数据目录

| 变量 | 默认值 | 说明 |
|------|--------|------|
| `A2X_REGISTRY_HOME` | 见下方解析顺序 | 数据根目录，`database/`、`llm_apikey.json`、`cluster_state.json`、`auth_data/` 都相对它解析 |
| `A2X_REGISTRY_AUTH_DATA` | `<HOME>/auth_data` | 鉴权数据（凭据哈希 + 审计日志）目录的显式覆盖。部署到只读包目录时用它把数据落到可写卷。详见 [auth_design.md](auth_design.md) |
| `A2X_REGISTRY_CLUSTER_STATE` | `<HOME>/cluster_state.json` | 集群身份文件路径的显式覆盖。同机跑多实例时必须每个实例设不同值 |

`A2X_REGISTRY_HOME` 的解析顺序见 `a2x_registry/common/paths.py`：环境变量 → `~/.a2x_registry/` → 当前目录。

## 2. 分布式同步（`cluster/`，默认关闭）

| 变量 | 默认值 | 说明 |
|------|--------|------|
| `A2X_REGISTRY_CLUSTER_ADVERTISE` | 空 | 对端回连本节点用的 base URL，必须填**对端可达**的地址（同机调试可用 `127.0.0.1`）。不设则不启用集群 |
| `A2X_REGISTRY_CLUSTER_KEEPALIVE_INTERVAL` | `10` | 直链保活广播周期（秒） |
| `A2X_REGISTRY_CLUSTER_HOLD_TIMEOUT` | `30` | 直链静默多久断会话并驱逐其记录（秒）；**失活耗时 ≈ 此值** |
| `A2X_REGISTRY_CLUSTER_ANTI_ENTROPY_INTERVAL` | `20` | 反熵对账 + GC 周期（秒） |
| `A2X_REGISTRY_CLUSTER_HTTP_TIMEOUT` | `5` | 单次对端调用超时（秒） |
| `A2X_REGISTRY_CLUSTER_BROADCAST_WORKERS` | `32` | 并发广播 / 保活线程池上限 |
| `A2X_REGISTRY_CLUSTER_MERKLE_BUCKETS` | `256` | Merkle 反熵桶数，**须全网一致** |

除 `ADVERTISE` 外，上表其余 6 项由 `ClusterConfig.from_env()` 按 `A2X_REGISTRY_CLUSTER_<字段大写>` 自动派生（见 `a2x_registry/cluster/config.py`）——**给 `ClusterConfig` 新增字段就会自动多一个变量**，改动时记得同步本表。非法值会打 warning 并回落默认。

部署步骤与拓扑示例见 [README_forDistributed.md](../README_forDistributed.md)，机制设计见 [cluster_design.md](cluster_design.md)。

## 3. 并发度调优

| 变量 | 默认值 | 说明 |
|------|--------|------|
| `A2X_REGISTRY_SEARCH_WORKERS` | `4` | `/api/search` 请求处理线程池上限 |
| `A2X_REGISTRY_DATASET_WORKERS` | `2` | `/api/datasets` CRUD / build 请求处理线程池上限 |
| `A2X_REGISTRY_LLM_WORKERS` | `20` | A2X 递归导航并发调用 LLM 的线程数。**受 LLM 供应商速率限制约束**，免费额度建议调小 |
| `A2X_REGISTRY_AGENT_CARD_WORKERS` | `10` | 注册时并发抓取 agent card URL 的线程数 |

前两个是模块级线程池，在 import 时创建，因此必须在**启动 server 前**设好。非法值会打 warning 并回落默认。

## 4. 前端

| 变量 | 默认值 | 说明 |
|------|--------|------|
| `A2X_FRONTEND_DIST_DIR` | 无 | 前端构建产物目录。设置后 backend 用 `StaticFiles` 挂载它。`ui/launcher.py` 在检测到 `ui/frontend/dist/` 时会自动设置，一般无需手工配置 |

## 5. 第三方 / 系统变量

| 变量 | 说明 |
|------|------|
| `HF_HOME` | HuggingFace 缓存目录，影响 `[vector]` extras 的嵌入模型下载位置。默认 `~/.cache/huggingface` |
| `NO_PROXY` | 本机访问（含集群同机调试）需要包含 `127.0.0.1,localhost`，否则请求会被系统代理拦截 |

## 6. 一体机模式

一体机（appliance）部署引入的 `A2X_REGISTRY_MODE` / `A2X_REGISTRY_BIND` / `A2X_REGISTRY_PORT` / `A2X_REGISTRY_DB_*` 系列变量尚未合入本分支，将在后续版本随一体机能力一起提供。
