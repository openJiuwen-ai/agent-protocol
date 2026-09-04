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

环境要求：Python 3.10 或更高版本。安装命令：

```bash
uv sync
```

| 运行环境 | 可运行示例 | 环境与交互要求 |
| --- | --- | --- |
| Windows | Ed25519 非交互示例；WebAuthn 笔记 demo；无 UI 冒烟测试 | WebAuthn 笔记 demo 需要配置 Windows Hello，并使用支持 WebAuthn 的浏览器（Chrome, Edge 等均支持） |
| macOS | Ed25519 非交互示例；WebAuthn 笔记 demo；无 UI 冒烟测试 | WebAuthn 笔记 demo 需要浏览器能够调用 passkey 或兼容的安全密钥 |
| Linux | Ed25519 非交互示例；无 UI 冒烟测试 | 示例不需要浏览器；由于无浏览器，暂时无法运行交互式 WebAuthn demo |

三个示例的用途如下：

- `examples/ed25519_authorization.py`：最小 Ed25519 注册与 operation 授权全链路，
  只需基础依赖；
- `examples/note_mcp_a4p`：带浏览器确认的 WebAuthn/passkey 桌面 demo，需要安装
  `demo` 额外依赖；
- `examples/note_mcp_a4p/smoke_test.py`：使用测试签名器覆盖笔记 MCP 的
  prepare/complete 流程，不启动 Web UI，需要安装 `demo` 额外依赖。


### 最小 Ed25519 注册与授权全链路示例

运行最小 Ed25519 注册与授权全链路：

```bash
uv run python examples/ed25519_authorization.py
```

这是一条非交互链路，不需要桌面环境、浏览器或硬件认证器。
脚本会在本机回环地址启动临时 A4P HTTP Server，生成临时
Ed25519 私钥，依次完成凭据注册、operation mandate 准备、用户签名和授权完成，
然后输出类似：

```json
{
  "credentialId": "cred_xxx",
  "approved": true,
  "operationId": "op_xxx"
}
```

进程执行完毕后会停止临时 HTTP Server。示例使用内存凭据存储且只生成临时私钥；
生产私钥应由调用方自己的密钥系统管理，SDK 不负责持久化或解锁。


### 笔记 MCP 服务器示例（基于 WebAuthn 的签名）

在 Windows 上运行本示例前，请进入“设置 > 账户 > 登录选项”，至少配置一种
Windows Hello 登录方式（PIN、面部识别或指纹识别）。浏览器认证器会使用该登录方式
在调用用户私钥签名前确认用户身份。

仅有终端的 Linux 环境无法完成需要用户确认的浏览器 WebAuthn 签名过程。

运行 `examples/note_mcp_a4p` 中的笔记 MCP 服务器示例前，需要额外安装 demo 依赖：

```bash
uv sync --extra demo
```

运行示例需要三个终端。

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

### 笔记 MCP 服务器无 UI 冒烟测试

该示例用于快速验证笔记 MCP Server 与 A4P 的 prepare/complete 集成，无需启动浏览器
Web UI，不需要 passkey、硬件认证器或人工确认。脚本使用临时 Ed25519 测试凭据和
本机 A4P HTTP Server，依次验证：

- Operation Mandate 可以授权删除指定笔记；
- 当前操作与已签名 mandate 参数不一致时拒绝删除；
- 一个 Intent Token 可以按授权范围删除多条笔记；
- Intent Token 缺少参数、包含额外参数或 action 不匹配时验证失败；
- 未完成授权前不会执行删除。

先安装 demo 额外依赖：

```bash
uv sync --extra demo
```

然后在仓库根目录运行：

```bash
uv run python examples/note_mcp_a4p/smoke_test.py
```

脚本会在 `127.0.0.1` 的 `18960` 至 `18962` 端口依次启动并停止临时服务，因此不依赖
外部 A4P Server，但运行时这些端口需要可用。全部检查通过时，最后一行输出：

```text
note_mcp_a4p smoke tests passed
```

该示例可在 Windows、macOS、Linux 环境中运行。

### 运行测试

```bash
uv run python -m pytest -q
```

## 更多文档

- [A4P 协议说明](A4P_PROTOCOL.md)
- [SDK 设计说明](A4P_SDK_DESIGN.md)
- [A4P + MCP 示例](examples/note_mcp_a4p/README.md)
