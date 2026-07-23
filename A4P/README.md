# A4P Python SDK

## 摘要

A4P（Agentic Authentication, Authorization, and Audit Protocol）是一套面向 AI Agent 的授权协议，具有授权细粒度、可追溯、抗篡改的安全特性。

本仓库是 A4P 协议的 Python SDK，用于在 Agent 执行敏感操作前获取用户授权，并通过可验证的 mandate（授权对象）和 token（令牌）传递授权范围。

当前版本：`0.2.0`

## 主要特性

- 两类授权模型
  - 操作授权（Operation Mandate）精确授权一次具体操作及参数。
  - 意图授权（Intent Mandate/Token）授权一组可复用的 action 范围。
- 清晰的角色分工
  - Agent 发起授权并负责消息转发，不参与授权安全判断。
  - A4P Server 创建并验证 Mandate 和 Token，维护服务端授权状态。
  - User Authorizer 运行在用户设备本地，验证授权内容并收集用户确认。
  - Tool Server 保护实际业务操作，只在 A4P 授权验证通过后执行工具调用。
- 可验证的授权链路
  - A4P Server 使用 Ed25519 签署 Mandate 和 Intent Token，保护授权内容及授权范围不被篡改。
  - 本地 User Authorizer 使用预置信任公钥验证 Mandate 的 Server 签名，只向用户展示经过验证的授权内容。
  - 使用 WebAuthn/passkey 时，User Authorizer 从经过验证的 Server-signed Mandate 派生 challenge，将用户验证和设备签名绑定到本次授权内容。
- 细粒度范围与额度控制
  - 支持参数精确匹配、通配符和额外参数控制。
  - 支持最大执行次数，并通过 SQLite 持久化、原子消费执行额度。
- 可插拔的用户签名载体
  - 每个 A4P Server 实例显式配置一个 signature method，内置 `ed25519` 和 `webauthn`。
  - 通用 credential registry 与方法专属注册解耦；Ed25519 一次注册，WebAuthn 两阶段注册。
- 轻量集成方式
  - Intent 和 Operation 统一采用 prepare/complete 两阶段接口，支持授权服务与用户确认异步完成。
  - Operation 授权由受保护的 Tool Server 发起和完成；Intent 授权由 Agent 发起，授权结果在后续工具调用中验证。
  - SDK 内置异步 HTTP 客户端、轻量 HTTP 服务端和可定制的授权展示文本，便于嵌入现有 Agent 与工具服务。


## 快速开始

环境要求：Python 3.10 或更高版本。

```bash
uv sync --locked
```

### 最小 Ed25519 注册与授权全链路示例

运行最小 Ed25519 注册与授权全链路：

```bash
uv run python examples/ed25519_authorization.py
```

该示例只生成临时私钥。生产私钥由调用方自己的密钥系统管理；SDK 不负责持久化或解锁。


### 笔记 MCP 服务器示例（基于 WebAuthn 的签名）

运行 `examples/note_mcp_a4p` 中的笔记 MCP 服务器示例前，需要额外安装 demo 依赖：

```bash
uv sync --locked --extra demo
```

示例需要三个终端。

终端 1，启动 A4P Server：

```bash
uv run python examples/note_mcp_a4p/run_authorization_server.py
```

终端 2，启动基于 WebAuthn 的 User Authorizer：

```bash
uv run python examples/note_mcp_a4p/run_user_authorizer.py
```

首次运行时，在终端 3 显式注册 WebAuthn/passkey 凭据：

```bash
uv run python examples/note_mcp_a4p/register_browser_key.py
```

脚本从 A4P Server 获取 registration options，交给本地 User Authorizer 调用浏览器认证器生成公钥/私钥，再把公钥返回 A4P Server 保存。

终端 3，启动模拟 Agent（意图授权模式）：

```bash
uv run python examples/note_mcp_a4p/agent_simulator.py --mode intent
```

模拟的 Agent 会申请一个允许删除笔记的 Intent Mandate。请在浏览器页面审核其 action 和参数范围；批准后，A4P Server 会签发可验证的 Intent Token。Agent 随后复用该 token 删除已列出的多条笔记，每次执行前都会校验授权范围并原子消费执行额度。

如需体验针对单次具体操作的精确授权，可将启动参数改为 `--mode operation`。

> 示例和内置默认密钥仅用于本地开发。生产环境必须使用 HTTPS、匹配的 WebAuthn RP ID/origin、独立签名密钥、受保护的本地信任配置。

## 更多文档

- [A4P 协议说明](A4P_PROTOCOL.md)
- [SDK 设计说明](A4P_SDK_DESIGN.md)
- [A4P + MCP 示例](examples/note_mcp_a4p/README.md)
