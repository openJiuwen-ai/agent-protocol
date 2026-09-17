# A4P + MCP 笔记管理示例

本示例展示了一个受 A4P 授权保护的笔记管理 MCP Server。

MCP Server 提供四个工具：

- `list_notes`
- `get_note`
- `add_note`
- `delete_note`

`delete_note` 被视为高风险操作，只有通过以下任一授权后才会执行删除：

- 精确授权本次笔记删除操作的 A4P Operation Mandate；或
- 允许删除笔记的 A4P Intent Token。

## 示例组件

| 组件 | 文件 | 职责 |
| --- | --- | --- |
| A4P Server | `run_authorization_server.py` | 生成和验证 Mandate、管理 WebAuthn credential、签发并验证 Intent Token |
| Note MCP Server | `note_mcp_server.py` | 提供笔记工具；在执行 `delete_note` 前调用 A4P |
| User Authorizer | `run_user_authorizer.py` | 本地验证 Server-signed Mandate、展示授权内容并调用浏览器 WebAuthn |
| 凭据注册客户端 | `register_browser_key.py` | 在 A4P Server 与 User Authorizer 之间转发一次 WebAuthn 注册流程 |
| 模拟 Agent | `agent_simulator.py` | 调用 MCP 工具并转发授权请求与用户签名结果 |

授权组件之间的主要关系是：

```text
Agent <-> Note MCP Server <-> A4P Server
  |
  +---- mandate + signingOptions ----> User Authorizer
  <----------- signedMandate ---------+
```

User Authorizer 不直接连接 A4P Server。Agent 转发给它的授权请求只包含
`mandate` 和 `signingOptions`；授权展示以 Server-signed Mandate 中的
`displayText` 和结构化授权字段为准。

## 安装

在 A4P SDK 根目录运行：

```bash
uv sync --locked --extra demo
```

## 运行前约定

示例默认使用以下地址：

| 配置 | 默认值 | 覆盖方式 |
| --- | --- | --- |
| A4P Server | `http://127.0.0.1:8961` | `A4P_SERVER_BASE_URL` |
| User Authorizer | `http://localhost:8970` | `A4P_USER_AUTHORIZER_BASE_URL` |
| Intent Token 用量数据库 | `.a4p/intent_token_usage.sqlite3` | `A4P_USAGE_DB_PATH` |

WebAuthn RP ID 为 `localhost`，预期 origin 为 `http://localhost:8970`。如果修改
User Authorizer 的地址，必须同步修改 A4P Server 中的 `expected_origin`；如果连
hostname 也发生变化，还必须同步修改 RP ID。

### Windows 环境

在 Windows 上运行本示例前，请进入“设置 > 账户 > 登录选项”，至少配置一种
Windows Hello 登录方式（PIN、面部识别或指纹识别）。浏览器认证器会使用该登录方式在调用用户私钥签名前确认用户身份。

### Linux 环境

仅有终端的 Linux 环境无法完成需要用户确认的浏览器 WebAuthn 签名过程，可在仓库根目录改为运行 Ed25519 非交互示例或 MCP 笔记服务器的无 UI 冒烟测试：

```bash
uv run python examples/ed25519_authorization.py
uv run python examples/note_mcp_a4p/smoke_test.py
```

## 启动 A4P Server 和 User Authorizer

终端 1，启动 A4P Server：

```bash
uv run python examples/note_mcp_a4p/run_authorization_server.py
```

该示例从 `a4p.user_signature.webauthn` 导入并显式配置
`WebAuthnSignatureMethod`。一个 Server 实例只启用一种用户签名方法，不会同时
接受 WebAuthn 和 Ed25519。

Server 启动时还会把公开验签配置写入 `.a4p/trusted_server_keys.json`，供本地 User Authorizer 固定信任当前 `serverId + keyId`。该文件不包含私钥。

未配置 `INTENT_SERVER_ED25519_PRIVATE_KEY` 和
`OPERATION_SERVER_ED25519_PRIVATE_KEY` 时，示例使用 SDK 内置的开发签名密钥并输出
高风险告警。内置密钥只能用于本地演示，不能用于生产部署。

终端 2，启动 A4P User Authorizer：

```bash
uv run python examples/note_mcp_a4p/run_user_authorizer.py
```

打开终端输出的本地 Web UI 地址，通常为：

```text
http://localhost:8970/
```

User Authorizer 不连接 A4P Server。它只接收 Agent 转发的 mandate 和 WebAuthn options，并在打开浏览器前使用 `.a4p/trusted_server_keys.json` 验证 Server 签名、challenge 内容绑定和 options 一致性。

浏览器页面模板、样式和 WebAuthn JavaScript 位于 `user_authorizer_assets/`，由本地 User Authorizer 通过固定资源白名单提供；Python service 不再内嵌前端代码。

如果只希望在终端输出 URL，而不自动打开浏览器，可运行：

```bash
uv run python examples/note_mcp_a4p/run_user_authorizer.py --no-open-browser
```

## 注册浏览器凭据

演示注册的浏览器凭据由 A4P Server 保存到：

```text
.a4p/webauthn_credentials.json
```

Intent Token 的执行次数通过 SQLite 原子持久化到：

```text
.a4p/intent_token_usage.sqlite3
```

可通过 `A4P_USAGE_DB_PATH` 指定其他 SQLite 文件路径。

Web UI 使用 `http://localhost:8970`，因为浏览器会将 localhost 视为可用于 WebAuthn 开发的安全上下文。生产部署必须使用 HTTPS，并配置匹配的 RP ID 和 origin。授权期间由 Agent 转发 mandate、signing options 和 assertion；注册期间由独立 enrollment 脚本转发 registration options 和公开 credential。私钥操作始终留在浏览器认证器中。注册 options 返回的 `registrationRequestId` 会随 credential 一起回传，确保同一用户的并发注册 challenge 彼此隔离。

首次运行时，在独立终端显式执行 enrollment：

```bash
uv run python examples/note_mcp_a4p/register_browser_key.py
```

该命令仅演示转发结构。生产环境必须使用强登录态保护凭据注册接口，
并处理 CSRF、防滥用和审计；脚本中配置的 `userId` 本身不构成身份证明。

## 使用单次操作授权运行

确保 A4P Server、User Authorizer 已启动且浏览器凭据已注册，然后启动模拟 Agent：

```bash
uv run python examples/note_mcp_a4p/agent_simulator.py --mode operation
```

模拟 Agent 会先列出全部笔记，然后首次调用 `delete_note`。MCP Server 根据当前 `note_id` 生成 operation，向 A4P Server prepare，再返回 `authorization_required`、mandate 和 signing options。Agent 取得 User Authorizer 签名后，以 `operation_authorization: {signedMandate}` 重新调用 `delete_note`。MCP Server 会再次从当前 `note_id` 生成 operation 并调用 A4P complete，验证通过后才删除笔记。

Prepare 会一次返回 Server-signed mandate 和 assertion options。Agent 将二者转发到本地；User Authorizer 先验 Server 签名和有效期，再从 mandate 重算并覆盖 WebAuthn challenge。本地校验失败时不会打开批准页面或调用认证器。

Agent 转发给 User Authorizer 的请求结构为：

```json
{
  "mandate": {},
  "signingOptions": {
    "signatureMethod": "webauthn",
    "methodOptions": {
      "allowCredentials": [],
      "challenge": "...",
      "rpId": "localhost",
      "timeout": 60000,
      "userVerification": "required"
    }
  }
}
```

Mandate 内签名保护的 `userAuthorization.methodPolicy.userVerification` 是安全策略，
`signingOptions.methodOptions.userVerification` 是传给浏览器 WebAuthn API 的执行参数。
User Authorizer 会重新派生 challenge，并强制 `userVerification=required`，不会直接信任
Agent 转发的动态 options。

Operation 授权精确对应一次工具调用：

```json
{
  "operation": {
    "action": "delete_note",
    "params": {
      "note_id": "note-1"
    }
  }
}
```

Complete 成功后返回的关键字段为：

```json
{
  "approved": true,
  "operationId": "op_xxx",
  "verificationResult": {
    "valid": true
  }
}
```

成功结果中的 `operationId` 是业务系统的幂等键。本示例只在进程内记录成功结果；
生产业务系统必须在执行副作用时以 `operationId` 实现持久化幂等。

## 使用 Intent 授权运行

保持 A4P Server 和 User Authorizer 运行，以 Intent 模式启动模拟 Agent：

```bash
uv run python examples/note_mcp_a4p/agent_simulator.py --mode intent
```

模拟 Agent 会申请一个允许删除笔记的 A4P Intent Token，将 mandate 交给 User Authorizer 获取用户签名，再通过 A4P Server 完成授权。取得 token 后，Agent 会复用它删除已列出的笔记。

Intent 授权使用可复用的 action 参数约束：

```json
{
  "intent": {
    "actions": [
      {
        "name": "delete_note",
        "params": {
          "note_id": "*"
        }
      }
    ]
  }
}
```

Intent complete 成功后返回 `approved + intentToken + verificationResult`。Intent Token
自身包含受 Server 签名保护的 `issuedAt` 和 `expireAt`；后续每次删除操作由 MCP
Server 调用 A4P Server 验证 token、agent、user、action 和 params。

## 冒烟测试

冒烟测试在 prepare 和 complete 之间使用测试签名器批准 mandate，不需要启动 Web UI：

```bash
uv run python examples/note_mcp_a4p/smoke_test.py
```

测试会验证：

- Operation challenge 经用户签名后可以删除一条笔记；
- 二次请求的 operation 参数与 challenge 不一致时拒绝删除；
- 一个 Intent Token 可以删除多条笔记；
- Intent Token 缺少参数、包含额外参数或 action 不一致时验证失败；
- 首次调用只返回 challenge，不执行删除。
