# A2X Registry — Agent Team 场景快速启动

**v0.1.6**

本文档是针对 **Agent Team 动态组队** 场景的简化版本，仅包含启动一个空白后端所需的最少步骤。后续所有服务注册、查询、预订锁等操作均通过客户端SDK完成，服务端无需任何预置数据或 LLM 配置。

## 安装与启动

### 精简安装（默认）

0.1.6 起 `pip install` **默认就是 Agent Team 精简版**——只装 SDK 必需的 5 个轻量包（`requests` / `fastapi` / `pydantic` / `python-multipart` / `uvicorn[standard]`），不再附带 `numpy` / `sentence-transformers` / `chromadb` 等数百 MB 的搜索/索引依赖。

从 GitCode 克隆 `agent-protocol` 的 `feature/Agentregistry` 分支安装：

```bash
git clone -b feature/Agentregistry https://gitcode.com/openJiuwen/agent-protocol.git
cd agent-protocol
pip install -e .
```

要求 Python ≥ 3.10。安装完成后即可使用 `a2x-registry` 启动注册中心。

### 精简版能力范围

- ✅ **可用**：所有 SDK 接口（数据集 CRUD、agent 注册/注销/更新/状态、按字段查询、整卡覆盖、预订锁 reservation lease、`/embedding-models` 元数据、`/build/status` 状态查询）。

如果同一环境之后想启用全量功能，运行 `pip install -e '.[full]'` 安装额外依赖然后**重启 `a2x-registry`** 即可，无需改任何代码。

## 启动后端

根据 Agent Team 的部署形态，选择对应启动方式：

**单机部署**（所有 Agent 与注册中心在同一台机器上）：

```bash
a2x-registry                    # 默认监听 127.0.0.1:8000
```

客户端 SDK 的 `base_url` 填 `http://127.0.0.1:8000`。

**分布式部署**（Agent 分散在多台机器上，需要跨机访问注册中心）：

```bash
a2x-registry --host 0.0.0.0              # 监听所有网卡，默认端口 8000
a2x-registry --host 0.0.0.0 --port 8080  # 指定端口
```

分布式部署下各端需要关注：

1. **后端机器**：用 `ip addr`（Linux/macOS）或 `ipconfig`（Windows）查出局域网 IP（如 `192.168.1.10`）；公网部署则用公网 IP 或域名
2. **防火墙 / 安全组**：放行监听端口（默认 8000）的入站流量
3. **客户端 SDK**：`base_url` 填 `http://<后端 IP 或域名>:<端口>`，例如 `http://192.168.1.10:8000`
4. **生产环境**（可选）：建议前面挂 nginx 反向代理 + HTTPS，客户端使用 `https://registry.yourdomain.com`

后端启动后，即可使用注册中心客户端 SDK 进行相关操作。
