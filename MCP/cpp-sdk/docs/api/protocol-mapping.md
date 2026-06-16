# MCP 协议方法 ↔ C++ API 对照

## Client 发起的请求

| MCP 方法 | C++ API（`McpClient`） |
|----------|-------------------------|
| `initialize` | `Initialize()` |
| `ping` | `SendPing()` |
| `tools/list` | `ListTools(cursor)` |
| `tools/call` | `CallTool(name, arguments, ...)` |
| `resources/list` | `ListResources(cursor)` |
| `resources/read` | `ReadResource(uri)` |
| `resources/subscribe` | `SubscribeResource(uri)` |
| `resources/unsubscribe` | `UnsubscribeResource(uri)` |
| `resources/templates/list` | `ListResourcesTemplates()` |
| `prompts/list` | `ListPrompts()` |
| `prompts/get` | `GetPrompt(name, arguments)` |
| `completion/complete` | `Complete(ref, arg, context)` |
| `logging/setLevel` | `SetLoggingLevel(level)` |

## Client 发起的通知

| MCP 通知 | C++ API |
|----------|---------|
| `notifications/roots/list_changed` | `SendRootsListChanged()` |
| `notifications/progress` | `SendProgressNotification(...)` |

## Server 注册的能力（由应用实现）

| MCP 方法 | Server 注册 API |
|----------|-----------------|
| `tools/list` / `tools/call` | `AddTool` / `RemoveTool` |
| `prompts/list` / `prompts/get` | `AddPrompt` / `RemovePrompt` |
| `resources/list` / `resources/read` | `AddResource` / `RemoveResource` |
| `resources/templates/list` | `AddResourceTemplate` / `RemoveResourceTemplate` |
| `completion/complete` | `AddCompletion` |
| `logging/setLevel` | `RegisterSetLoggingLevelHandler` |

## Server 向 Client 发起的请求（Client 侧回调）

| MCP 方法 | C++ API（`McpClient`） |
|----------|-------------------------|
| `roots/list` | `SetListRootsCallback` |
| `sampling/createMessage` | `SetSamplingCreateMessageCallback` |
| `elicitation/create` | `SetElicitCallback` / `SetElicitUrlCallback` |

## Server 向 Client 发起的通知

| MCP 通知 | Client 处理 |
|----------|-------------|
| `notifications/message` | `SetLoggingCallback`（协议日志，非 `MCP_LOG`） |
| `notifications/tools/list_changed` 等 | SDK 内部处理 / 日志 |
| `notifications/progress` | `CallTool` 的 `progressCallback` |

## Server 会话内主动调用（`McpServerSession`）

| MCP 方法 | C++ API |
|----------|---------|
| `roots/list` | `session->ListRoots()` |
| `sampling/createMessage` | `session->SamplingCreateMessage(params)` |
| `notifications/progress` | `session->SendProgressNotification(...)` |
| `notifications/*/list_changed` | `SendToolListChangedNotification()` 等 |

## 两套「日志」区别

| 概念 | API | 用途 |
|------|-----|------|
| SDK 诊断日志 | `MCP_LOG`、`SetLogCallback` | 调试 SDK 与应用程序 |
| MCP 协议日志 | `SetLoggingCallback`、`notifications/message` | 服务端推送给客户端的日志消息 |

## 示例索引

| 协议能力 | 示例路径 |
|----------|----------|
| Tool / Initialize | `example/client_example/tool_example/` |
| Prompt | `example/client_example/prompt_example/` |
| Resource | `example/client_example/resource_example/` |
| Sampling | `example/client_example/sampling_example/` |
| Server 全功能 | `example/server_example/` |
