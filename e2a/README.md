# E2A 协议包

## 摘要

E2A（Everything-to-Agent）是 Gateway 与 AgentServer 之间的统一载荷协议：经 Gateway 规范化后的请求与 Agent 返回的每一条出站记录，都使用同一套信封表达，通道差异在 Gateway 入口被映射为规范化字段。

本目录是 E2A 协议的 Python 共享数据包：jiuwenswarm 与 jiuwenswarm-gateway 共用同一套 dataclass / enum / 常量，保证两侧对线协议的理解一致。

当前版本：`0.1.0`（协议版本 `E2A_PROTOCOL_VERSION`，默认 `1.0`）

包内只包含纯数据结构与纯函数：

- 不含行为实现与接口 ABC（`AgentServerClient`、扩展基类、`CryptoProvider` 等由各仓实现侧自行持有）；
- 不 import jiuwenswarm / openjiuwen，不产生反向依赖；
- 无第三方运行时依赖，可被多个仓库同时引用。

## 主要特性

- 统一请求与响应模型
  - 请求：`E2AEnvelope`；响应：`E2AResponse`；两者字段命名对称（`snake_case`、RFC 3339 `timestamp`、共用 `E2AProvenance`）。
  - 关联 id 分工明确：`request_id` 用于网关↔AgentServer 与流式 chunk 对齐，`jsonrpc_id` 回显 ACP JSON-RPC `id`，`correlation_id` 承载追踪。
- 单一业务参数入口
  - 业务参数全部放在 `params`（与 JSON-RPC `params` 对齐）：用户正文、多模态块、附件等不再使用顶层 `payload`。
  - `from_dict` 仍兼容遗留的顶层 `payload`，其键会合并进 `params`（不覆盖已有键）。
- 双语义 `method`
  - 网关 RPC 名（`chat.send`、`history.get`、`chat.interrupt` 等）与 ACP JSON-RPC method（`session/prompt`、`initialize` 等）共用该字段，来源由 `provenance.source_protocol` 区分。
  - 自定义扩展使用 `method="ext"` + `ext_method`。
- 响应分层与流式不变量
  - L1 信封（`response_id` / `request_id` / `sequence` / `is_final` / `status`）、L2 判别载荷（`response_kind` + `body`）、L3 可选 `projections`。
  - 同一 `request_id` 下 `sequence` 从 0 严格递增；正常结束时恰好一条 `is_final=true`。
  - `response_kind` 取值以 `constants.E2A_RESPONSE_KINDS` 为准，`status` 以 `E2A_RESPONSE_STATUS_*` 为准。
- ACP / A2A 互操作
  - `adapters` 提供 ACP JSON-RPC、A2A SendMessage 到 E2A 的转入，以及 E2A 到 ACP JSON-RPC、A2A `StreamResponse` 的导出。
  - `merge_params_to_acp_prompt` 仅在 `method == "session/prompt"` 时由 `content_blocks` 或 `text` / `content` / `query` 补全 `prompt`。
- 旧载荷兼容
  - 只读兼容键：`channel_id`→`channel`、`req_method`→`method`、`metadata`→`channel_context`、`binding`→`provenance`。
  - `wire_codec` 同时解析 E2A 响应帧与旧版 `AgentResponse` 帧，便于迁移期混跑。
- 明确的模块边界

| 模块 | 职责 |
| --- | --- |
| `models` | `E2AEnvelope`、`E2AResponse`、`E2AProvenance`、`E2AFileRef`、`E2AAuth` 及 `from_dict` / `to_dict` |
| `constants` | 协议版本、`E2A_SOURCE_PROTOCOL_*`、`E2A_RESPONSE_KIND*`、`E2A_RESPONSE_STATUS_*`、ACP method / session update 元组 |
| `adapters` | ACP / A2A → E2A，E2A → ACP JSON-RPC，响应到 ACP / A2A 的投影 |
| `agent_models` / `agent_compat` | 旧 `AgentRequest` / `AgentResponse` 线协议类型与兼容转换 |
| `gateway_normalize` / `wire_codec` | 网关侧归一化纯函数；AgentServer↔Gateway 线上编解码 |
| `message` | WebSocket / E2A 报文与枚举（`Message`、`ReqMethod`、`EventType`、`Mode`） |
| `identity` | 请求身份字段契约与 routing / `user_id` 归一化纯函数 |
| `channel_event` / `hooks` / `audit` / `auth` / `logging_config` | 通道、hook、审计、SSH 认证、日志配置的字段契约 |
| `cron_models` / `redis_keys` / `session_assets` / `template_slots` / `provider_domains` | cron 数据结构、Gateway Redis key 模板、会话素材、企业 `template_ref` 槽位、provider 能力域名 |
| `expert_http_routes` / `runtime_http_routes` / `package_allowlist` / `mcp_oauth` / `match_expressions` | 专家包与 Runtime HTTP 路径表、专家包允许名单、MCP OAuth 回调契约、授权表达式 |
| `sdk` | 扩展 SDK 共享数据类（`ApplicationPluginServices`、`FrontendContribution` 等） |
| `acp` | ACP JSON-RPC 与会话更新转换的细粒度辅助 |

## 快速开始

环境要求：Python 3.11 至 3.13（`requires-python = ">=3.11,<3.14"`），无第三方运行时依赖。

在仓库内开发：

```bash
cd e2a
uv sync --group dev
```

其他仓库引用时，按 git 子目录方式声明（`rev` 固定到具体 commit）：

```toml
dependencies = ["e2a"]

[tool.uv.sources]
e2a = { git = "https://gitcode.com/openJiuwen/agent-protocol.git", rev = "<commit>", subdirectory = "e2a" }
```

```bash
pip install "e2a @ git+https://gitcode.com/openJiuwen/agent-protocol.git@<commit>#subdirectory=e2a"
```

### 构造与解析请求信封

```python
from e2a import E2AEnvelope

envelope = E2AEnvelope(
    request_id="req-1",
    session_id="sess-1",
    channel="web",
    method="chat.send",
    is_stream=True,
    user_id="u-1",
    params={"content": "你好", "mode": "agent", "work_mode": "work"},
)
envelope.ensure_timestamp()

wire = envelope.to_dict()
assert E2AEnvelope.from_dict(wire).request_id == "req-1"
```

`params` 是本包唯一约定的业务参数字典；附件推荐使用 `E2AFileRef` 形状（`uri` 必填），放在 `params.files` 或 `params.attachments` 中。

### 构造流式响应

```python
from e2a import (
    E2A_RESPONSE_KIND_E2A_CHUNK,
    E2A_RESPONSE_STATUS_IN_PROGRESS,
    E2AResponse,
)

chunk = E2AResponse(
    response_id="r1",
    request_id="req-1",
    sequence=0,
    response_kind=E2A_RESPONSE_KIND_E2A_CHUNK,
    status=E2A_RESPONSE_STATUS_IN_PROGRESS,
    body={"delta_kind": "text", "delta": "你好"},
)
chunk.ensure_timestamp()
```

结束帧使用 `E2A_RESPONSE_KIND_E2A_COMPLETE` 或 `E2A_RESPONSE_KIND_E2A_ERROR`，并置 `is_final=True`。

### ACP JSON-RPC 互转

```python
from e2a import envelope_from_acp_jsonrpc, envelope_to_acp_jsonrpc_call

envelope = envelope_from_acp_jsonrpc(
    "session/prompt",
    {"session_id": "s1", "prompt": [{"type": "text", "text": "hi"}]},
    jsonrpc_id=42,
)
call = envelope_to_acp_jsonrpc_call(envelope)
assert call["method"] == "session/prompt"
```

### 运行测试

```bash
cd e2a
uv run python -m pytest -q
```

## 更多文档

- [E2A 协议说明（中文）](https://gitcode.com/openJiuwen/agent-gateway/blob/develop/docs/zh/reference/e2a-protocol.md)
- [E2A 协议说明（English）](https://gitcode.com/openJiuwen/agent-gateway/blob/develop/docs/en/reference/e2a-protocol.md)
- [仓库总览](https://gitcode.com/openJiuwen/agent-protocol/blob/develop/README.md)

字段与取值的最终实现以 `models.py`、`constants.py` 为准；本文与实现不一致时，以代码为准并同步更新本文。
