# A2X Registry — 智能体服务发现注册中心

**v0.3.2**

## 概述

A2X Registry 是一个 **Agent 及 Agent 可调用服务的注册中心**，同时内置 **Agent 原生的高效服务发现方案（A2X 搜索）**。

作为注册中心，它提供服务全生命周期的常规管理能力：注册 / 注销、按字段查询、整卡覆盖与字段级更新、状态声明（online / busy / offline）、基于所有权的权限控制，以及 Agent Team 场景下的短时预订锁（reservation lease），便于多 agent 在同一池子里互相协调。数据集隔离、Embedding 模型可选、多种服务类型（generic / A2A / Skill）并存。

特色功能是 A2X 搜索，用于解决智能体互联网时代的核心问题：**如何让智能体从海量服务中高效、准确地找到所需能力。** 现有方案各有局限 —— MCP 全量上下文溢出、"Lost in the Middle" 效应；向量检索不理解跨术语意图；查询改写方向不确定。A2X 通过自动构建 **层次化能力目录**（分类树）+ **LLM 递归语义导航**，由 LLM 沿"领域 → 子领域 → 具体能力 → 服务"逐层逼近目标，查找成本接近 O(log N)，兼顾召回与精度。分类树由 LLM 从服务描述中全自动构建，无需人工维护。

## 评估结果

### [ToolRet_clean](https://github.com/Weizheng96/A2X-registry-demo-data/tree/main/ToolRet_clean)（1839 服务 · 1714 查询）

数据来源：[tool-retrieval-benchmark](https://github.com/mangopy/tool-retrieval-benchmark)，经过数据清洗。

| 方法 | Hit Rate | Recall | Precision | Avg Tokens/q | Avg LLM Calls | 数据 |
|------|:--------:|:------:|:--------:|:------------:|:-------------:|:----:|
| **A2X** (v0.1.1) | **92.59%** | **89.19%** | 16.06% | 7,069 | 7.96 | [summary](results/20260323_a2x-getall_toolretnew_1714/summary.json) |
| Vector (top-5) | 69.08% | 61.81% | 15.24% | 0 | 0 | [summary](results/20260323_vector_toolretnew_1714/summary.json) |
| Traditional (MCP)* | 86.00% | 83.67% | 5.17% | 66,568 | 1.00 | [summary](results/20260323_traditional_toolretnew_50/summary.json) |

\* Traditional 方案仅使用 name + description。若加入完整 inputSchema，单次查询 Token 消耗将达到 ~200k，已超出大多数模型的上下文窗口限制。

### [publicMCP](https://github.com/Weizheng96/A2X-registry-demo-data/tree/main/publicMCP)（1387 MCP 服务 · 50 查询）

数据来源：[MCP 官方服务器列表](https://github.com/modelcontextprotocol/servers)，共 1387 条 MCP 服务器描述。查询模拟真实用户请求（含个人偏好、多服务组合等）。

| 方法 | Hit Rate | Recall | Precision | Avg Tokens/q | Avg LLM Calls | 数据 |
|------|:--------:|:------:|:--------:|:------------:|:-------------:|:----:|
| **A2X** (v0.1.1) | **100%** | **94.87%** | 10.54% | 15,366 | 14.10 | [summary](results/20260323_a2x-getall_publicmcp_50/summary.json) |
| Vector (top-5) | 72.0% | 42.77% | 22.00% | 0 | 0 | [summary](results/20260323_vector_publicmcp_50/summary.json) |
| Vector (top-10) | 78.0% | 50.50% | 13.20% | 0 | 0 | [summary](results/20260323_vector_publicmcp_50/summary.json) |

> **关于 Precision**：在大规模服务库中，ground truth 难以标注所有与请求相关的服务。人工抽样检查发现超过 60% 的假阳选项实际上与请求功能相关，因此 Precision 指标显著低估了实际检索质量，本文不引用该指标。

## 快速开始

### 1. 安装

> 依赖布局：默认 `pip install` 仅装 SDK 必需的轻量依赖；**A2X 搜索 / Traditional 搜索 / A2X 分类构建** 都是纯 LLM 工作流，**默认安装即可使用**（只要配好 LLM API key）；只有**向量检索**才需要 `[vector]` extras（数百 MB ML 栈）。本 README 介绍**全量模式**（含向量检索 + 评估 CLI），需要附加 `[full]`。Agent Team 精简部署见 [README_forAgentTeam.md](README_forAgentTeam.md)。

从 GitCode 克隆 `agent-protocol` 的 `feature/Agentregistry` 分支并安装依赖：

```bash
git clone -b feature/Agentregistry https://gitcode.com/openJiuwen/agent-protocol.git
cd agent-protocol
pip install -e '.[full]'
```

要求 Python ≥ 3.10。`pip install -e '.[full]'` 会自动安装全部依赖并注册 `a2x-registry` 命令行入口。

`[full]` 等同于 `[vector,evaluation]`：

| extras | 包含 | 启用功能 |
|---|---|---|
| 默认 `pip install` | requests / fastapi / pydantic / python-multipart / uvicorn[standard] | 注册中心 SDK + **A2X 搜索 + Traditional 搜索 + A2X 分类构建**（任何 LLM-only 能力） |
| `[vector]` | + numpy / sentence-transformers / chromadb | + 向量检索（`POST /api/search` 的 `method=vector`） |
| `[evaluation]` | + tqdm | + `a2x-evaluate-*` 评估 CLI 的进度条 |
| `[full]` | 上述全部 | 全量功能（含向量检索 + 评估 CLI） |

### 2. 配置 LLM API（可选，A2X 搜索和分类树构建必需）

默认位置 `~/.a2x_registry/llm_apikey.json`（Windows 下 `C:\Users\<你>\.a2x_registry\llm_apikey.json`）。参考 `a2x_registry/llm_apikey.example.json` 模板：

```json
{
  "providers": [
    {
      "name": "deepseek",
      "base_url": "https://api.deepseek.com/chat/completions",
      "model": "deepseek-chat",
      "api_keys": ["sk-your-deepseek-api-key"]
    }
  ]
}
```

支持多 provider 配置，按顺序轮询使用，兼容所有 OpenAI-compatible API。

> 仅使用向量检索 / 通用 CRUD 时不需要此配置。
> 如需把 key 文件放到别处，设 `A2X_REGISTRY_HOME=/your/path` 环境变量即可。

### 3. 下载演示数据集（可选）

项目提供预构建的演示数据集（含服务描述、分类树、评估查询）：

```bash
# pip 用户：克隆到默认位置
git clone https://github.com/Weizheng96/A2X-registry-demo-data.git ~/.a2x_registry/database

# 源码用户：克隆到项目根
git clone https://github.com/Weizheng96/A2X-registry-demo-data.git database
```

包含以下数据集：

| 数据集 | 服务数 | 语言 | 说明 |
|--------|:-----:|:---:|------|
| `ToolRet_clean` | 1839 | EN | [tool-retrieval-benchmark](https://github.com/mangopy/tool-retrieval-benchmark) 清洗版 |
| `publicMCP` | 1387 | EN | [MCP 官方服务器列表](https://github.com/modelcontextprotocol/servers) |
| `ToolRet_clean_CN` | 1839 | ZH | ToolRet_clean 中文翻译版 |
| `publicMCP_CN` | 1387 | ZH | publicMCP 中文翻译版 |
| `default` | — | — | 21 个公开 A2A Agent（启动后自动拉取） |
| `publicSkill` | 17 | EN | Claude Code Skill 集合 |

> 不下载也可以正常使用，通过 UI 或 API 创建自己的数据集并注册服务。

### 4. 启动

两种启动方式二选一。方式 A 仅起后端（走 HTTP API 或本地配置），方式 B 同时起后端和网页 UI。

**方式 A：常规启动（仅后端）**

```bash
a2x-registry                    # http://127.0.0.1:8000，docs 在 /docs
a2x-registry --port 8080        # 换端口
a2x-registry --host 0.0.0.0     # 开放到局域网
```

**方式 B：带前端启动（后端 + 网页 UI，仅源码安装可用）**

```bash
python ui/launcher.py
```

launcher 自带后端，无需额外跑方式 A。启动模式根据 `ui/frontend/dist/` 是否存在自动判断：

| 情况 | 行为 | 访问地址 |
|------|------|----------|
| 有 `dist/` | 后端直接托管静态文件，无需 Node.js | http://localhost:8000 |
| 无 `dist/`（首次） | 自动启动 Vite 开发服务器（需 Node.js） | http://localhost:5173 |

构建前端生产版本：`cd ui/frontend && npm install && npm run build`

### 5. 使用

#### 方式一：网页 UI

启动时选择了方式 B 即可打开浏览器使用。UI 提供两个模式：

- **搜索模式** — 交互对比 A2X / 向量 / MCP 的检索效果，D3.js 实时动画展示分类树导航过程
- **管理员模式** — 数据集管理、服务注册/注销（含 Skill 文件夹上传）、服务查询、分类树构建、Embedding 模型配置

**UI 演示视频：**

https://github.com/Weizheng96/A2X-registry-demo-data/raw/doc/ui_demo.mp4

> 注：演示中注销阶段灰色不可选的服务是通过 `user_config.json` 注册的，不支持单独注销。

#### 方式二：HTTP Fast API

直接通过 HTTP 调用 `a2x-registry` 暴露的接口。

**数据集管理**：

```bash
# 列出数据集
curl http://localhost:8000/api/datasets

# 创建数据集（指定 Embedding 模型）
curl -X POST http://localhost:8000/api/datasets \
     -H "Content-Type: application/json" \
     -d '{"name": "my_dataset", "embedding_model": "all-MiniLM-L6-v2"}'

# 删除数据集
curl -X DELETE http://localhost:8000/api/datasets/my_dataset
```

**服务注册/注销**：

```bash
# 注册通用服务
curl -X POST http://localhost:8000/api/datasets/my_dataset/services/generic \
     -H "Content-Type: application/json" \
     -d '{"name": "天气查询", "description": "查询城市天气和预报"}'

# 注册 A2A Agent（通过 URL 自动拉取 Agent Card）
curl -X POST http://localhost:8000/api/datasets/my_dataset/services/a2a \
     -H "Content-Type: application/json" \
     -d '{"agent_card_url": "https://agent.example.com/.well-known/agent.json"}'

# 注册 A2A Agent（直接提供 Agent Card）
curl -X POST http://localhost:8000/api/datasets/my_dataset/services/a2a \
     -H "Content-Type: application/json" \
     -d '{"agent_card": {"name": "翻译助手", "description": "支持中英日韩多语言互译", "url": "https://translate.example.com/a2a"}}'

# 注册 Skill（上传 ZIP）
curl -X POST http://localhost:8000/api/datasets/my_dataset/skills \
     -F "file=@my_skill.zip"

# 注销服务
curl -X DELETE http://localhost:8000/api/datasets/my_dataset/services/{service_id}

# 注销 Skill（移至 removed_skills/）
curl -X DELETE http://localhost:8000/api/datasets/my_dataset/skills/{skill_name}

# 下载 Skill（ZIP）
curl -O http://localhost:8000/api/datasets/my_dataset/skills/{skill_name}/download

# 浏览服务
curl http://localhost:8000/api/datasets/my_dataset/services?mode=browse

# 查询单个服务（skill 类型返回 ZIP）
curl http://localhost:8000/api/datasets/my_dataset/services?mode=single&service_id={id}
```

**分类树构建**：

```bash
# 触发构建（后台运行）
curl -X POST http://localhost:8000/api/datasets/my_dataset/build \
     -H "Content-Type: application/json" -d '{}'

# 查看构建状态
curl http://localhost:8000/api/datasets/my_dataset/build/status

# 实时日志流（SSE）
curl http://localhost:8000/api/datasets/my_dataset/build/stream
```

**搜索**：

```bash
# 同步搜索
curl -X POST http://localhost:8000/api/search \
     -H "Content-Type: application/json" \
     -d '{"query": "帮我预订航班", "method": "a2x_get_all", "dataset": "my_dataset"}'
```

搜索方法：`a2x_get_all`（所有相关服务）、`a2x_get_one`（最相关的服务）、`a2x_get_important`（同类服务去重）、`vector`（向量检索）、`traditional`（MCP 全量）。

A2X 搜索支持 WebSocket 流式返回（`/api/search/ws`），实时推送分类导航步骤。

**Embedding 模型配置**：

```bash
# 查看支持的模型
curl http://localhost:8000/api/datasets/embedding-models

# 查看/切换数据集的 Embedding 模型
curl http://localhost:8000/api/datasets/my_dataset/vector-config
curl -X POST http://localhost:8000/api/datasets/my_dataset/vector-config \
     -H "Content-Type: application/json" \
     -d '{"embedding_model": "shibing624/text2vec-base-chinese"}'
```

支持 3 种 Embedding 模型：

| 模型 | 维度 | 适用语言 |
|------|:---:|:---:|
| `all-MiniLM-L6-v2` | 384 | 英文（默认） |
| `shibing624/text2vec-base-chinese` | 768 | 中文 |
| `paraphrase-multilingual-MiniLM-L12-v2` | 384 | 多语言 |

### 鉴权（可选）

默认完全开放：任何调用方均可注册 / 改 / 删。可选启用内置静态 API Key 鉴权模块。

启用一次（生成第一个管理员 token，stderr 一次性打印）：

```bash
a2x-registry auth init
```

之后创建需要鉴权的 namespace 时显式声明：

```bash
curl -X POST http://localhost:8000/api/datasets \
     -H "Authorization: Bearer <admin token>" \
     -H "Content-Type: application/json" \
     -d '{"name": "my_protected_ns", "auth_required": true}'
```

未带 `auth_required` 的 namespace 无需 token。三档角色（admin / provider / user）+ namespace 作用域设计详见 [docs/auth_design.md](docs/auth_design.md)。

客户端 SDK 通过 `~/.a2x_registry_client/cli_token.json` 配置文件读取 token（由 `a2x-registry-client login` 写入），详见 [client/README.md](client/README.md)。

### 心跳保活（可选）

默认行为：服务一旦注册永久存在。可选启用内置心跳模块，让客户端通过定期心跳维持服务存活，停跳后自动清理。

启用某个 namespace：

```bash
curl -X POST http://localhost:8000/api/datasets/my_ds/lease-config \
     -H "Content-Type: application/json" \
     -d '{"enabled": true, "min_ttl": 10, "max_ttl": 600, "grace_period": 60}'
```

之后注册到该 namespace 时声明 `lease_ttl`：

```bash
curl -X POST http://localhost:8000/api/datasets/my_ds/services/a2a \
     -H "Content-Type: application/json" \
     -d '{"agent_card": {...}, "lease_ttl": 60}'
```

服务必须每 `ttl/3` 秒发一次 `POST /api/datasets/my_ds/services/{sid}/heartbeat`，否则 `lease_ttl` 过期后被标记为不健康并最终自动清理。Grace 窗口期内重新心跳即可恢复，无需重新注册。

未带 `lease_config` 的 namespace 不接受 `lease_ttl`，注册的服务永久存在。状态机、4 角矩阵、客户端 `auto_renew` 详见 [docs/heartbeat_design.md](docs/heartbeat_design.md)。

> 完整 API 文档见 [docs/backend_api.md](docs/backend_api.md)，各模块内部接口见对应设计文档。

> 同时我们为 Agent Team 场景提供特化的 Python 客户端 SDK——与本仓**同库、同版本号、独立安装**（位于 [`client/`](client/README.md)）：
> ```bash
> pip install "a2x-registry-client @ git+https://gitcode.com/openJiuwen/agent-protocol.git@feature/Agentregistry#subdirectory=client"
> ```

#### 方式三：基于本地配置文档

不通过 API，直接在磁盘上编辑 `database/{数据集名}/user_config.json`，声明要注册的服务；下次启动时后端自动加载。

```json
{
  "services": [
    {
      "type": "generic",
      "name": "天气查询",
      "description": "根据城市名查询实时天气和未来预报",
      "url": "https://api.example.com/weather"
    },
    {
      "type": "a2a",
      "agent_card_url": "https://agent.example.com/.well-known/agent.json"
    },
    {
      "type": "a2a",
      "agent_card": {
        "name": "翻译助手",
        "description": "支持中英日韩多语言互译",
        "url": "https://translate.example.com/a2a",
        "skills": [
          {"name": "translate", "description": "将文本翻译为目标语言"}
        ]
      }
    }
  ]
}
```

支持四种注册对象：
- **generic** — 通用服务，提供 name + description
- **a2a (URL)** — 通过 `agent_card_url` 自动拉取 [A2A 协议](https://google.github.io/A2A/) Agent Card
- **a2a (内联)** — 通过 `agent_card` 直接提供 Agent Card 内容
- **skill (文件夹)** — 放入 `database/{数据集名}/skills/{skill名}/` 目录（需含 `SKILL.md`），或通过 API 上传 ZIP

## 文档

| 文档 | 内容 |
|------|------|
| [docs/architecture_overview.md](docs/architecture_overview.md) | **整体架构** — 模块布局、对外接口、数据流、安装模式（首页推荐） |
| [docs/backend_api.md](docs/backend_api.md) | 后端全量 API 接口说明（请求/响应格式、SSE 协议、503 契约） |
| [docs/frontend_design.md](docs/frontend_design.md) | 前端架构与 API 调用说明（搜索流程、WebSocket、SSE） |
| [docs/a2x_design.md](docs/a2x_design.md) | A2X 搜索算法设计 |
| [docs/build_design.md](docs/build_design.md) | 分类树自动构建设计 |
| [docs/register_design.md](docs/register_design.md) | 服务注册模块设计 |
| [docs/search_design.md](docs/search_design.md) | 搜索流程详细设计 |
| [docs/incremental_design.md](docs/incremental_design.md) | 增量构建设计 |
| [docs/auth_design.md](docs/auth_design.md) | 鉴权模块设计（静态 API Key + 三档角色 + namespace 作用域；默认关闭） |
| [docs/heartbeat_design.md](docs/heartbeat_design.md) | 心跳保活模块设计（per-namespace opt-in 租约；软驱逐 / 硬删两段式；默认关闭） |
| [docs/cluster_design.md](docs/cluster_design.md) | 分布式同步模块设计（多注册中心 gossip 同步 + 失活驱逐；默认关闭） |
| [README_forDistributed.md](README_forDistributed.md) | 分布式部署指南（两节点示例，照做即可） |

## 适用场景

A2X 可索引任何带有 `description` 字段的智能体服务，包括：垂域智能体、MCP 工具、Agent Skill、以及可被智能体调用的社会服务与资源。

- **互联网级 Agent DNS**：作为海量智能体与服务之上的统一能力发现层
- **组织级网关**：在内部工具、部门服务、数据接口之间实现高效的能力发现与路由