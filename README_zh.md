# Agent Protocol

[English](./README.md) | 简体中文

## 简介

**Agent Protocol** 是 [openJiuwen](https://github.com/openJiuwen-ai) 生态中面向智能体互操作的协议 SDK 与服务注册中心集合，帮助开发者连接智能体与工具、与远端智能体通信、发现可用服务，在多个 LLM 部署间智能路由请求，并在执行敏感操作前落实可验证的用户授权。

智能体应用除了模型和任务逻辑，还需要解决外部能力接入、协作对象发现、消息与任务结果交换，以及代理用户执行操作时的权限边界等问题。本仓库通过 **MCP C++ SDK**、**A2A C++ SDK**、**A2X Registry**、**A4P Python SDK**、**A4P Java SDK** 和 **IntelliRouter** 六个互补组件提供支持。开发者可以按需独立使用，也可以结合具体业务进行组合集成。

## 核心能力

### 基于 MCP 的工具与资源接入

Model Context Protocol（MCP）C++ SDK 提供客户端和服务端能力，支持 **Streamable HTTP** 与 **stdio** 传输。开发者可以通过 MCP Server 暴露工具、资源和提示词，也可以构建客户端连接已有 MCP 服务。SDK 同时支持鉴权、Sampling、进度通知和 Roots 等能力。

### 基于 A2A 的智能体通信

Agent2Agent（A2A）C++ SDK 支持基于 **HTTP 与 JSON-RPC** 的智能体间通信。客户端可以发现 Agent Card、发送消息、查询或取消任务，以及接收流式事件；服务端可以注册 `AgentExecutor`、管理任务生命周期并暴露 JSON-RPC 端点，同时支持请求拦截器、自定义任务存储等扩展方式。

### 基于 A2X 的服务注册与发现

A2X Registry 管理智能体及其可调用的服务，支持注册与注销、按字段查询、服务卡片更新、online/busy/offline 状态管理、基于所有权的访问控制，以及用于 Agent Team 协同的短时预订锁。

在能力发现方面，A2X 搜索结合自动构建的**层次化能力目录**与 **LLM 语义导航**，帮助智能体根据自然语言需求逐层定位服务。注册中心还提供可选的向量检索、数据集隔离、HTTP API、Python 客户端，以及用于服务管理和搜索探索的 Web UI。A2X 搜索和目录构建需要配置 LLM，基础注册管理操作则不需要。

### 基于 A4P 的可验证授权

Agentic Authentication, Authorization, and Audit Protocol（A4P）Python SDK 提供面向具体操作的 **Operation Mandate（操作授权）**，以及允许在指定操作类型和参数范围内多次执行的 **Intent Mandate/Token（意图授权与令牌）**。通过区分 Agent、A4P Server、本地 User Authorizer 和受保护的 Tool Server，将授权安全判断独立于智能体的决策逻辑。

SDK 提供 Ed25519 签名的授权对象、Ed25519 与 WebAuthn/passkey 用户签名方式、参数范围校验，以及基于 SQLite 持久化和原子消费的执行次数限制。prepare/complete 两阶段接口支持异步完成用户确认，便于集成到现有工具服务中。

### 基于 A4P Java 的支付意图授权

A4P Java SDK 当前的 SDK 构件版本为 `0.1.0`，实现的支付意图协议报文版本为 `1.0`，通过 L1/L2/L3 签名链完成智能体创建、意图中心签发和用户授权。SDK 支持意图激活、真实性验证与撤销，以及支付预留、完成和取消，并校验金额、执行次数、商品白名单和周期额度等约束。

SDK 提供不可变数据模型、规范化 JSON 序列化、执行上下文序列化与恢复、可插拔签名和验签接口，以及可选的 RSA-3072 适配器。执行账本保存在内存中，应用需自行实现持久化、数据库加锁、防重放和密钥与身份绑定。集成细节请参阅 [Java SDK 指南](./A4P-java-sdk/README_zh.md)。

### 基于 IntelliRouter 的智能 LLM 路由

IntelliRouter 是一个轻量级 **LLM 代理 SDK**，将同一模型名映射到多个部署端点，并对每次请求进行智能路由。它支持池化管理多家 Provider 的部署——OpenAI、Anthropic、Bedrock、DeepSeek、Gemini、SiliconFlow，以及任意 OpenAI 兼容端点——并通过可插拔策略选择部署：SimpleShuffle、LowestLatency、TagBased、TokenAware、RateLimitAware、Adaptive、OrderedFailover 与 TagFiltered。

路由器持续跟踪各部署的健康与配额状态（延迟、失败计数、TPM/RPM 用量、冷却窗口），后台运行健康检查，请求失败时自动重试与故障转移，并通过事件总线、Prometheus 指标、Web 看板和日志钩子提供可观测性。SDK 同时内置流式响应与 JSON/Markdown 输出解析器。

## 组件选型

各组件面向不同的接入需求，可独立使用，无需同时引入其他组件。

| 接入需求 | 对应组件 | 在应用中的作用 |
| --- | --- | --- |
| 访问工具、资源和提示词 | MCP C++ SDK | 通过 MCP 客户端与服务端连接智能体和外部能力。 |
| 委派任务给其他智能体 | A2A C++ SDK | 交换消息、跟踪任务状态并接收流式结果。 |
| 寻找合适的服务或协作对象 | A2X Registry | 注册服务元数据、发现能力并协调服务可用状态。 |
| 在用户批准的范围内执行操作 | A4P Python SDK | 获取可验证的授权，并在受保护操作执行前校验授权范围。 |
| 在批准的意图范围内授权并执行支付 | A4P Java SDK | 验证签名链，在执行全生命周期内落实支付约束。 |
| 在多个 LLM 部署间智能路由请求 | IntelliRouter | 池化模型端点，按请求选择最优部署，失败时自动切换。 |

## 仓库结构

```text
agent-protocol/
├── MCP/cpp-sdk/          # MCP C++ 客户端/服务端 SDK 与示例
├── A2A/cpp-sdk/          # A2A C++ 客户端/服务端 SDK 与示例
├── AgentRegistry/        # A2X 注册中心、Python 客户端、Web UI 与文档
├── A4P-python-sdk/       # A4P Python SDK、协议文档与示例
├── A4P-java-sdk/         # A4P Java 支付意图 SDK、规范化 JSON 规则与示例
└── intelli_router/       # IntelliRouter LLM 代理 SDK、测试与示例
```

## 快速开始

克隆仓库后，根据需要接入的组件阅读相应指南：

```bash
git clone https://github.com/openJiuwen-ai/agent-protocol.git
cd agent-protocol
```

| 组件 | 环境要求 | 入门路径 |
| --- | --- | --- |
| [MCP C++ SDK](./MCP/cpp-sdk/README.md) | C++17、CMake ≥ 3.15 及系统依赖；Linux/macOS | 安装依赖、构建 SDK，运行工具调用的客户端与服务端示例。 |
| [A2A C++ SDK](./A2A/cpp-sdk/README.md) | C++17、CMake ≥ 3.15 及系统依赖；Linux 为主要验证平台 | 启用示例构建，运行 Hello World 与流式通信示例。 |
| [A2X Registry](./AgentRegistry/README.md) | Python ≥ 3.10；A2X 搜索需要配置 LLM | 安装并启动注册中心，通过 HTTP API 或 UI 注册和查询服务。 |
| [A4P Python SDK](./A4P-python-sdk/README.md) | Python ≥ 3.10 与 uv | 安装依赖，运行无需交互的 Ed25519 授权全链路示例。 |
| [A4P Java SDK](./A4P-java-sdk/README_zh.md) | JDK ≥ 17、Maven ≥ 3.9 | 在 `A4P-java-sdk` 中运行 `mvn test` 和 `mvn package`，再按 SDK 指南运行 `PaymentExample`。 |
| [IntelliRouter](./intelli_router/README.md) | Python ≥ 3.9 | 安装 SDK，运行单元测试，体验端到端与可观测性示例。 |

各组件分别构建和安装，完整依赖与各平台操作步骤请参阅对应指南。

## 文档导航

- **MCP：** [API 参考](./MCP/cpp-sdk/docs/api/README.md) · [依赖说明](./MCP/cpp-sdk/docs/dependencies.md) · [测试说明](./MCP/cpp-sdk/docs/testing.md)
- **A2A：** [API 参考](./A2A/cpp-sdk/docs/api/README.md) · [依赖说明](./A2A/cpp-sdk/docs/dependencies.md) · [错误处理](./A2A/cpp-sdk/docs/errors.md)
- **A2X：** [注册中心指南](./AgentRegistry/README.md) · [Python 客户端](./AgentRegistry/client/README.md) · [Agent Team 部署](./AgentRegistry/README_forAgentTeam.md)
- **A4P：** [协议说明](./A4P-python-sdk/A4P_PROTOCOL.md) · [SDK 设计](./A4P-python-sdk/A4P_SDK_DESIGN.md) · [MCP 集成示例](./A4P-python-sdk/examples/note_mcp_a4p/README.md)
- **A4P Java：** [SDK 指南](./A4P-java-sdk/README_zh.md) · [规范化 JSON 与签名字节](./A4P-java-sdk/docs/canonical-json-spec.md) · [支付示例](./A4P-java-sdk/src/main/java/io/openjiuwen/a4p/examples/PaymentExample.java)
- **IntelliRouter：** [SDK 指南](./intelli_router/README.md) · [API 参考](./intelli_router/API.md) · [示例](./intelli_router/examples/)

## 参与贡献

我们欢迎所有形式的贡献，包括但不限于：

- 提交问题和功能建议
- 改进文档
- 提交代码
- 分享使用经验

## 开源许可证

本项目依据 Apache-2.0 许可证授权。

本产品仅作为流程编排工具，不包含 AI 模型能力；用户在连接 AI 模型用于特定业务场景时，需自行承担欧盟 AI 法案等相关合规义务。
