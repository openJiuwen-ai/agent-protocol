# E2A 协议包

## 摘要

E2A（Everything-to-Agent）是面向 Agent 交互的统一载荷协议：规范化后的请求与 Agent 返回的每一条出站记录，都使用同一套信封表达，接入通道的差异在入口被映射为规范化字段。

本目录是 E2A 协议的 Python 共享数据包。发行名为 `openjiuwen-e2a`，导入名保持 `e2a`。

当前版本：`0.3.0`（协议版本 `E2A_PROTOCOL_VERSION`，默认 `1.0`）

## 主要特性

- 统一请求与响应模型：`E2AEnvelope` / `E2AResponse`，字段命名对称，共用来源信息。
- 单一业务参数入口：用户正文、多模态块、附件等全部放在 `params`，与 JSON-RPC `params` 对齐。
- 双语义 `method`：同时支持 RPC 方法名与 ACP JSON-RPC method；自定义扩展使用 `method="ext"` + `ext_method`。
- 流式响应：支持 chunk、complete、error 三类响应。
- ACP / A2A 互操作：提供 ACP JSON-RPC、A2A 报文与 E2A 之间的双向转换。
- 旧载荷兼容：兼容遗留顶层 `payload` 与旧字段键，便于平滑迁移。

## 快速开始

环境要求：Python 3.11 至 3.13（`requires-python = ">=3.11,<3.14"`），无第三方运行时依赖。

在仓库内开发：

```bash
cd openjiuwen-e2a
uv sync --group dev
```

其他仓库引用时，按 git 子目录方式声明（`rev` 固定到具体 commit）：

```toml
dependencies = ["openjiuwen-e2a"]

[tool.uv.sources]
openjiuwen-e2a = { git = "https://gitcode.com/openJiuwen/agent-protocol.git", rev = "<commit>", subdirectory = "openjiuwen-e2a" }
```

```bash
pip install "openjiuwen-e2a @ git+https://gitcode.com/openJiuwen/agent-protocol.git@<commit>#subdirectory=openjiuwen-e2a"
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
cd openjiuwen-e2a
uv run python -m pytest -q
```

## 更多文档

- [E2A 协议说明（中文）](https://gitcode.com/openJiuwen/agent-gateway/blob/develop/docs/zh/reference/e2a-protocol.md)
- [E2A 协议说明（English）](https://gitcode.com/openJiuwen/agent-gateway/blob/develop/docs/en/reference/e2a-protocol.md)
- [仓库总览](https://gitcode.com/openJiuwen/agent-protocol/blob/develop/README.md)

字段与取值以 `models.py`、`constants.py` 实现为准。
