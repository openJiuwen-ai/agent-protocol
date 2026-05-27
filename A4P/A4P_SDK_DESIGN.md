# A4P SDK 设计与接口总结

本文档基于当前独立 Python SDK 源码整理，覆盖协议对象、客户端、服务端、HTTP 网关、签名校验原语以及代码检查发现的问题。

## 1. SDK 定位

A4P SDK 提供一套本地授权协议原语，用于在 Agent 执行敏感能力前引入用户授权边界。代码中有两类授权模型：

- **Intent authorization**：面向一段时间内的意图授权。服务端创建 `intent-mandate`，用户签名同意后，服务端再签发短期 `intent-token`，后续操作通过 token 校验 `action + params`。
- **Operation authorization**：面向单次具体操作授权。服务端创建 `operation-mandate`，用户签名后返回 signed mandate，后续直接校验该 mandate 是否精确匹配一次 `action + params` 调用。

整体设计偏轻量：协议数据使用 `dict[str, Any]` 和 frozen dataclass 表达，签名使用 HMAC-SHA256，HTTP 层使用标准库 `urllib` 和 `asyncio.start_server`，不引入外部 Web 框架。

## 2. 模块结构

| 模块 | 职责 |
| --- | --- |
| `pyproject.toml` | 独立 Python package 元数据，使用 `src/` layout，包名为 `a4p`。 |
| `src/a4p/types.py` | 定义公开请求/响应 dataclass、mandate/token dataclass，以及 `to_payload()` 序列化工具。 |
| `src/a4p/intent_mandate.py` | 创建、规范化、签名、验证 intent mandate；签发并验证 intent token；校验 action/params 约束。 |
| `src/a4p/operation_mandate.py` | 创建、规范化、签名、验证 operation mandate；校验一次具体 action/params 调用。 |
| `src/a4p/user_authorizer.py` | 定义用户授权边界 `A4PUserAuthorizer`，默认实现 `RejectingA4PUserAuthorizer`。 |
| `src/a4p/server.py` | 服务端编排层：创建 mandate、调用 user authorizer、验证签名、返回 token 或 signed mandate。 |
| `src/a4p/client.py` | 异步 HTTP 客户端：向 A4P HTTP server 发起授权/校验请求。 |
| `src/a4p/http_server.py` | 最小本地 HTTP server：把 HTTP POST endpoint 分发到 `A4PServer`。 |
| `src/a4p/security.py` | 集中处理签名 key 环境变量读取、生产模式默认 key 拒绝和开发模式高危告警。 |
| `src/a4p/__init__.py` | 导出 SDK 公开接口，使用包内绝对导入。 |

## 3. 核心数据模型

### 3.1 通用响应

`VerificationResult` 表示校验结果：

- `valid: bool`
- `reason?: str`
- `code?: str`
- `matchedScope?: dict`

提供两个便捷构造：

- `VerificationResult.ok(matched_scope=None)`
- `VerificationResult.fail(reason, code="AUTHORIZATION_INVALID")`

### 3.2 用户授权边界

`UserAuthorizationRequest`：

- `requestId`
- `mandate`
- `uiContext`

`UserAuthorizationResponse`：

- `requestId`
- `approved`
- `signedMandate`
- `rejectReason`
- `approvedAt`

`A4PUserAuthorizer` 是一个 async Protocol：

```python
class A4PUserAuthorizer(Protocol):
    async def authorize(self, request: UserAuthorizationRequest) -> UserAuthorizationResponse:
        ...
```

SDK 默认使用 `RejectingA4PUserAuthorizer`，即没有 UI/设备侧授权器时全部拒绝。

用户授权器实现只需要负责展示 mandate、收集用户决策，并在批准后返回用户签名后的 mandate。SDK 提供统一签名 helper：

```python
from a4p import sign_user_mandate_for_request

signed = sign_user_mandate_for_request(
    request.mandate,
    request_id=request.requestId,
)
```

该 helper 会根据 mandate type 自动分发到 intent 或 operation 的用户签名函数。`ApprovingA4PUserAuthorizer` 可用于测试和可信本地流程，它会自动批准并签名所有 mandate；生产 UI 不应绕过真实用户确认。

### 3.3 Intent 授权请求/响应

`IntentAuthorizationRequest`：

- `agentId`
- `userId`
- `intent`
- `validitySeconds`
- `metadata`

`intent` 期望包含：

- `actions: list[dict]`
- 每个 action 为 `{"name": "...", "params": {...}}`

`params` 约束支持：

- `"*"`：允许该 action 使用任意参数。
- `{}`：该 action 不允许携带参数。
- `{"note_id": "*"}`：实际调用必须包含 `note_id`，值任意。
- `{"note_id": "note-1"}`：实际调用必须精确等于该值。

`IntentAuthorizationResponse`：

- `requestId`
- `mandate`
- `intentToken`
- `verificationResult`
- `approved`
- `rejectReason`

### 3.4 Intent mandate

`IntentMandate` wire type 为 `a4p/v1/intent-mandate`，核心字段：

- `mandateId`
- `server`
- `subject`: 通常是 `{"type": "agent", "id": "agent:{agentId}"}`
- `intent`: `{"actions": [{"name": "delete_note", "params": {"note_id": "*"}}]}`
- `validTime`: `{"start": "...Z", "end": "...Z"}`
- `displayText`
- `signatures.server`
- `signatures.user`

签名采用 canonical JSON，即 `json.dumps(..., sort_keys=True, separators=(",", ":"))` 后 HMAC-SHA256。

### 3.5 Intent token

`IntentToken` wire type 为 `a4p/v1/intent-token`，核心字段：

- `tokenId`
- `mandateId`
- `subject`
- `user`
- `intent`: `actions[{name, params}]`
- `issuedAt`
- `expireAt`
- `nonce`
- `signature`
- `alg`
- `keyId`

token 使用从 server signing key 和 `mandateId` 派生出的 token signing key 签名，`keyId` 格式为 `server#intent-token-v1:{mandateId}`。

### 3.6 Operation 授权请求/响应

`OperationAuthorizationRequest`：

- `agentId`
- `userId`
- `operation`
- `validitySeconds`
- `metadata`

`operation` 常见字段：

- `action`
- `params`

`OperationAuthorizationResponse`：

- `requestId`
- `mandate`
- `signedMandate`
- `verificationResult`
- `approved`
- `rejectReason`

### 3.7 Operation mandate

`OperationMandate` wire type 为 `a4p/v1/operation-mandate`，核心字段：

- `operationId`
- `server`
- `subject`
- `operation`
- `validTime`: `{"until": "...Z", "displayUntil": "...北京时间", "timezone": "Asia/Shanghai"}`
- `displayText`
- `signatures.server`
- `signatures.user`

operation mandate 要求 `operation.action` 和 `operation.params` 与实际执行请求精确一致。

## 4. Intent 授权流程

1. 调用方提交 `IntentAuthorizationRequest`。
2. `A4PServer.authorize_intent()` 创建 server-signed intent mandate。
3. 服务端调用 `A4PUserAuthorizer.authorize()`，把 mandate 交给 UI/设备侧确认。
4. 用户授权器返回 `signedMandate`。
5. 服务端调用 `verify_intent_mandate()` 校验 server/user 签名、有效期、server id。
6. 校验通过后调用 `issue_intent_token()` 签发 intent token。
7. 后续调用 `verify_intent_token()` 校验 token 签名、有效期、agent/user/action/params。

常用原语接口：

```python
create_intent_mandate(...)
sign_server_mandate(mandate)
sign_user_intent_mandate(mandate, request_id=...)
verify_intent_mandate(mandate, expected_server=...)
issue_intent_token(mandate, user_id=...)
verify_intent_token(token, action=..., params=..., expected_agent_id=..., expected_user_id=...)
params_match_intent_token(token, action=..., params=...)
normalize_intent_mandate(mandate)
```

## 5. Operation 授权流程

1. 调用方提交 `OperationAuthorizationRequest`。
2. `A4PServer.authorize_operation()` 创建 server-signed operation mandate。
3. 服务端调用 `A4PUserAuthorizer.authorize()`。
4. 用户授权器返回 `signedMandate`。
5. 服务端调用 `verify_operation_mandate()` 校验签名、有效期和 expected operation。
6. 校验通过则返回 `signedMandate`，不额外签发 token。

常用原语接口：

```python
create_operation_mandate(...)
sign_server_mandate(mandate)
sign_user_mandate(mandate, request_id=...)
verify_operation_mandate(mandate, expected=...)
normalize_operation_mandate(mandate)
```

## 6. HTTP 接口

`A4PClient` 默认连接：

- base URL: `A4P_SERVER_BASE_URL`，默认 `http://127.0.0.1:8961`
- timeout: `A4P_HTTP_TIMEOUT_S`，默认 `300` 秒，最小 `1` 秒

公开方法：

```python
await A4PClient().request_intent_authorization(request)
await A4PClient().verify_intent_token(request)
await A4PClient().request_operation_authorization(request)
```

HTTP server endpoint：

| Method | Path | 作用 |
| --- | --- | --- |
| `POST` | `/a4p/v1/intent-authorizations` | 创建 intent mandate、请求用户授权、签发 intent token。 |
| `POST` | `/a4p/v1/intent-tokens/verify` | 校验 intent token 是否满足 expected action/params。 |
| `POST` | `/a4p/v1/operation-authorizations` | 创建 operation mandate、请求用户授权、返回 signed mandate。 |

HTTP server 环境变量：

- `A4P_SERVER_ENABLED`: `1/true/yes/on` 时视为启用
- `A4P_SERVER_HOST`: 默认 `127.0.0.1`
- `A4P_SERVER_PORT`: 默认 `8961`

## 7. 签名与安全模型

当前签名算法均为 HS256，即 HMAC-SHA256：

- intent mandate server key: `INTENT_SERVER_SIGNING_KEY`
- intent mandate user key: `INTENT_USER_SIGNING_KEY`
- operation mandate server key: `OPERATION_SERVER_SIGNING_KEY`
- operation mandate user key: `OPERATION_USER_SIGNING_KEY`

如果环境变量不存在，代码会退回到内置开发默认 key，并通过日志打印高危告警。该行为只用于本地开发；当 `A4P_ENV`、`APP_ENV`、`ENV` 或 `PYTHON_ENV` 为 `prod`/`production` 时，缺失 key 或显式配置为内置默认 key 都会抛出异常，拒绝继续使用不安全密钥。

## 8. 检查发现的问题

### P1: 内置开发签名 key 可能被误用于生产

`intent_mandate.py` 和 `operation_mandate.py` 在环境变量缺失时会使用硬编码默认 signing key。任何拿到源码的人都可以伪造本地默认 key 签名。

建议：生产模式下强制要求显式配置 key；至少在 server 初始化时检测默认 key 并打印高危告警或拒绝启动。

### P2: agent id 格式容易误用

mandate/token 的 subject id 被写为 `agent:{agentId}`，但 `verify_intent_token(... expected_agent_id=...)` 做的是完全相等比较。若调用方传入原始 `agentId`，例如 `"demo-agent"`，会和 token 中的 `"agent:demo-agent"` 不匹配。

建议：统一 API 文档中的 agent id 语义，或在 verify 层同时接受 raw id 与 `agent:` 前缀格式。

### P2: HTTP server 的错误语义过粗

`A4PHTTPServer._dispatch()` 对未知路径返回 `{"error": "not_found"}`，外层仍以 HTTP 200 返回。JSON 解析错误、请求行错误等也都会进入 500。

建议：未知路径返回 404；非法 JSON/请求格式返回 400；只把真正的服务端异常返回 500。

### P3: 缺少自动化测试

当前已补齐独立 `pyproject.toml` 和 `src/a4p` package layout，但还没有测试文件。

建议：补充至少以下测试：

- intent mandate 创建、用户签名、验证、token 签发、token 校验成功路径。
- intent action/params/agent/user mismatch 失败路径。
- operation mandate 创建、用户签名、expected operation 校验成功路径。
- operation params mismatch、过期、签名篡改失败路径。
- HTTP endpoint status code 和错误响应。

## 9. 建议优先级

1. 去除生产默认 signing key 风险，明确 key 配置策略。
2. 补齐最小测试集。
3. 改善 HTTP server 的状态码和错误分类。
4. 发布前补充包构建、安装、导入的 CI 检查。
