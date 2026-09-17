# A2A 协议方法 ↔ C++ API 对照

SDK 内部 JSON-RPC 方法名见 `src/shared/common_types.h`。下表列出与 A2A 协议及公开 Client API 的对应关系。

## Client 发起的请求

| JSON-RPC method（SDK） | A2A 协议能力 | C++ API（`A2A::Client::Client`） |
|----------------------|--------------|----------------------------------|
| `SendMessage` | message/send | `SendMessage`（`streaming=false`） |
| `SendStreamingMessage` | message/stream | `SendMessage`（`streaming=true`） |
| `GetTask` | tasks/get | `GetTask(params)` |
| `CancelTask` | tasks/cancel | `CancelTask(params)` |
| `SubscribeToTask` | tasks/resubscribe | `Resubscribe(params, ctx, handler)` |
| `CreateTaskPushNotificationConfig` | tasks/pushNotificationConfig/set | `SetTaskPushNotificationConfig` |
| `GetTaskPushNotificationConfig` | tasks/pushNotificationConfig/get | `GetTaskPushNotificationConfig` |
| `ListTaskPushNotificationConfigs` | tasks/pushNotificationConfig/list | `ListTaskPushNotificationConfigs` |
| `DeleteTaskPushNotificationConfig` | tasks/pushNotificationConfig/delete | `DeleteTaskPushNotificationConfig` |
| `GetAgentCard` | agent/getCard（SDK 扩展标识） | `GetCard()` |

> **Agent Card HTTP 路径（推荐）**：`GET /.well-known/agent-card.json`，经 `HttpCardResolverBuilder` / `A2ACardResolver::GetAgentCard()` 获取，无需 JSON-RPC。

## Server 侧（由 SDK 路由 + 应用实现）

| JSON-RPC method | Server 组件 |
|-----------------|-------------|
| `SendMessage` / `SendStreamingMessage` | `DefaultRequestHandler` → `AgentExecutor::Execute` |
| `GetTask` / `CancelTask` / `SubscribeToTask` | `TaskManager` + `AgentExecutor` |
| Push Notification 系列 | `PushNotificationConfigStore` + Client API 对应方法 |
| `GetAgentCard` | `JsonRpcHandler::OnGetAgentCard` + HTTP Agent Card 端点 |

应用侧主要实现 **`AgentExecutor`**（及可选 **`TaskStore`**），无需直接注册每个 JSON-RPC 方法。

## 流式响应事件类型

Server 通过 SSE / 流式 JSON 推送时，SDK 解析为 `ClientEvent`：

| 流类型常量 | 含义 |
|------------|------|
| `task` | 完整 Task 对象 |
| `status-update` | `TaskStatusUpdateEvent` |
| `artifact-update` | `TaskArtifactUpdateEvent` |

Client 侧通过 `SendMessage` 的 `ResponseHandler` 或 `AddEventConsumer` 接收。

## 运行时信息 vs 诊断日志

| 概念 | API | 用途 |
|------|-----|------|
| SDK 诊断日志 | `A2A_LOG`、`SetLogCallback` | 调试 SDK 与应用程序（默认 stdout，见 [logging.md](../logging.md)） |
| 协议运行时数据 | `ClientEvent`、`Message`、`A2AError`、任务状态事件 | 智能体业务消息、任务状态、对外错误 |

A2A **没有** MCP 式的 `notifications/message` 协议日志通道；任务进度与错误应通过上述协议事件传递。

## 示例索引

| 协议能力 | 示例路径 |
|----------|----------|
| 非流式 message/send | `examples/helloworld_client.cpp` / `helloworld_server.cpp` |
| 流式 message/stream | `examples/streaming_client.cpp` / `streaming_server.cpp` |
| Agent Card（HTTP） | `examples/helloworld_client.cpp`（`HttpCardResolverBuilder`） |
| JSON-RPC 冒烟 | `scripts/run_example.sh` |

## 相关文档

- [client.md](client.md)
- [server.md](server.md)
- [errors.md](../errors.md)
