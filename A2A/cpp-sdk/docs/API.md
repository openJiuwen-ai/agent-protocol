# A2A C++ SDK API 参考

本文档描述 SDK 对外公开头文件（`include/`）中的 API，与 [A2A 协议规范 v1.0](https://a2a-protocol.org/v1.0.0/specification/) 对齐。实现细节与内部头文件（`src/`）不在本文档范围内。

相关文档：[README.md](../README.md) · [LOGGING.md](LOGGING.md) · [TESTING.md](TESTING.md)

## 头文件索引

编译后头文件会复制到 `output/include/`，引用时添加该目录到 include path。

| 头文件 | 说明 |
|--------|------|
| `types.h` | 协议数据结构、错误码、AgentCard |
| `error.h` | 客户端/服务端 C++ 异常类型 |
| `a2a_log.h` | 日志级别与宏 |
| `client/client.h` | `Client` 接口与配置 |
| `client/client_factory.h` | 客户端工厂 |
| `client/a2a_card_resolver.h` | Agent Card 解析接口 |
| `client/http_card_resolver_builder.h` | HTTP Card 解析器构建 |
| `client/client_transport.h` | 传输层抽象 |
| `client/jsonrpc_transport.h` | JSON-RPC 传输实现 |
| `client/client_call_interceptor.h` | 请求拦截器 |
| `server/server.h` | 服务端生命周期接口 |
| `server/http_server_builder.h` | HTTP 服务端构建 |
| `server/agent_executor.h` | 业务执行器 |
| `server/task_updater.h` | 任务状态/产物更新 |
| `server/request_context.h` | 单次请求上下文 |
| `server/task_store.h` | 任务持久化接口 |
| `server/server_call_context.h` | 服务端调用上下文 |

---

## 协议类型（`types.h`）

命名空间：`A2A`

### 核心枚举

| 类型 | 说明 |
|------|------|
| `Role` | 消息角色：`USER` / `AGENT` / `UNSPECIFIED` |
| `TaskState` | 任务状态：`SUBMITTED`、`WORKING`、`INPUT_REQUIRED`、`COMPLETED`、`CANCELED`、`FAILED` 等 |
| `A2AErrorCode` | JSON-RPC 与 A2A 业务错误码（如 `TASK_NOT_FOUND`、`A2A_REQUEST_TIMEOUT`） |

### 消息与任务

| 类型 | 主要字段 | 说明 |
|------|----------|------|
| `Part` | `text`, `raw`, `url`, `data`, `mediaType` | 多模态消息片段 |
| `Message` | `messageId`, `role`, `parts`, `taskId`, `contextId` | 用户/智能体消息 |
| `Task` | `id`, `contextId`, `status`, `artifacts`, `history` | 任务实体 |
| `TaskStatus` | `state`, `message`, `timestamp` | 任务当前状态 |
| `Artifact` | `artifactId`, `parts`, `name` | 任务产物 |
| `MessageSendParams` | `message`, `configuration`, `metadata` | `message/send` 请求体 |
| `MessageSendConfiguration` | `acceptedOutputModes`, `historyLength`, `pushNotificationConfig` | 发送配置 |

### 事件类型（流式）

| 类型 | 说明 |
|------|------|
| `TaskStatusUpdateEvent` | 任务状态变更事件 |
| `TaskArtifactUpdateEvent` | 任务产物增量事件 |

### Agent Card

| 类型 | 主要字段 | 说明 |
|------|----------|------|
| `AgentCard` | `name`, `description`, `version`, `capabilities`, `skills`, `supportedInterfaces` | 智能体能力描述 |
| `AgentCapabilities` | `streaming`, `pushNotifications`, `extendedAgentCard` | 能力开关 |
| `AgentInterface` | `url`, `protocolBinding`, `protocolVersion` | 传输端点（如 JSON-RPC URL） |
| `AgentSkill` | `id`, `name`, `description`, `tags` | 技能描述 |

### 错误模型

| 类型 | 说明 |
|------|------|
| `A2AError` | 通用错误：`code`, `message`, `data` |
| `TaskNotFoundError` 等 | 预置错误结构，对应协议错误码 |

常量：

- `JSONRPC_TRANSPORT` = `"JSONRPC"`
- `JSONRPC_VERSION` = `"2.0"`

---

## 客户端 API

命名空间：`A2A::Client`

### `ClientConfig`

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `streaming` | `bool` | `true` | 是否使用流式 `message/stream` |
| `polling` | `bool` | `false` | 非流式时是否轮询 |
| `supportedTransports` | `vector<string>` | — | 客户端支持的传输（如 `{"JSONRPC"}`） |
| `useClientPreference` | `bool` | `false` | 传输选择是否优先客户端列表 |
| `acceptedOutputModes` | `optional<vector<string>>` | — | 接受的输出模式 |
| `pushNotificationConfigs` | `vector<PushNotificationConfig>` | — | 默认推送配置 |

### `ClientCallContext`

定义于 `types.h`，用于单次客户端请求的附加上下文（可选）：

| 字段 | 说明 |
|------|------|
| `state` | 任意 per-request 状态字符串 |
| `headers` | 序列化请求头 |

传入 `Client` 各方法的 `context` 参数；传 `nullptr` 表示无额外上下文。

### `Client` 接口

除 `Resubscribe` 返回 `void` 外，其余异步方法返回 `std::future`。`SendMessage` 与 `Resubscribe` 通过 `handler` 回调投递 `ClientEvent`。

| 方法 | JSON-RPC 方法 | 说明 |
|------|---------------|------|
| `SendMessage(msg, context, handler, timeout)` | `message/send` 或 `message/stream` | 发送消息；`handler` 接收 `ClientEvent` |
| `GetTask(params, context, timeout)` | `tasks/get` | 查询任务 |
| `CancelTask(params, context, timeout)` | `tasks/cancel` | 取消任务 |
| `SetTaskPushNotificationConfig(cfg, ...)` | `tasks/pushNotificationConfig/set` | 设置推送配置 |
| `GetTaskPushNotificationConfig(params, ...)` | `tasks/pushNotificationConfig/get` | 获取推送配置 |
| `ListTaskPushNotificationConfigs(params, ...)` | `tasks/pushNotificationConfig/list` | 列出推送配置 |
| `DeleteTaskPushNotificationConfig(params, ...)` | `tasks/pushNotificationConfig/delete` | 删除推送配置 |
| `Resubscribe(params, context, handler, timeout)` | — | 重新订阅任务事件流（`void`，同步注册 handler） |
| `GetCard(context, timeout)` | `GetAgentCard` | 获取 Agent Card |
| `AddEventConsumer(c)` | — | 注册全局事件消费者 |
| `AddRequestMiddleware(middleware)` | — | 添加请求拦截器 |
| `Close()` | — | 关闭连接，释放资源 |

### `ClientEvent`

`std::variant` 之一：

- `Message` — 单条消息响应
- `A2AError` — 错误
- `pair<Task, UpdateEvent>` — 任务及状态/产物更新（流式）

`UpdateEvent` 可为 `TaskStatusUpdateEvent` 或 `TaskArtifactUpdateEvent`。

### `ClientFactory`

```cpp
static std::shared_ptr<Client> Create(
    const AgentCard& card,
    const ClientConfig& config,
    const std::vector<Consumer>& consumers = {},
    const std::vector<std::shared_ptr<ClientCallInterceptor>>& interceptors = {});

static std::shared_ptr<Client> Create(
    const AgentCard& card,
    const ClientConfig& config,
    std::shared_ptr<ClientTransport> transport,
    const std::vector<Consumer>& consumers = {});
```

根据 `AgentCard.supportedInterfaces` 与 `ClientConfig` 自动选择传输并创建客户端。也可注入自定义 `ClientTransport`。

### `A2ACardResolver` / `HttpCardResolverBuilder`

```cpp
// 构建 HTTP 解析器
auto resolver = HttpCardResolverBuilder::Build(
    "http://127.0.0.1:8080",           // baseUrl
    "/.well-known/agent-card.json");   // card path

AgentCard card = resolver->GetAgentCard().get();
```

| 方法 | 说明 |
|------|------|
| `GetAgentCard(relativeCardPath)` | 获取单个 Agent Card |
| `GetAllAgentCards()` | 获取全部 Card（本地场景） |

### `ClientCallInterceptor`

请求发出前同步拦截，可修改 payload 与 headers：

```cpp
void Intercept(const std::string& methodName,
               std::string& payload,
               std::map<std::string, std::string>& headers,
               const AgentCard* agentCard,
               const ClientCallContext* context);
```

### `ClientTransport` / `JsonRpcTransport`

传输层抽象，一般通过 `ClientFactory` 使用。高级场景可自定义实现 `ClientTransport`，或用 `JsonRpcTransport` 直接构造并注入工厂。

---

## 服务端 API

命名空间：`A2A::Server`

### `Server`

| 方法 | 说明 |
|------|------|
| `Start()` | 启动监听，成功返回 `0` |
| `Stop()` | 停止服务 |

### `HttpConfig`

| 字段 | 默认值 | 说明 |
|------|--------|------|
| `ip` | — | 绑定 IP |
| `port` | — | 监听端口 |
| `ioThreadNum` | `1` | I/O 线程数 |
| `endpoint` | `"/jsonrpc"` | JSON-RPC 路径 |

### `HttpServerBuilder::Build`

```cpp
static std::shared_ptr<Server> Build(
    const HttpConfig& config,
    const AgentCard& agentCard,
    const AgentCard& extendedAgentCard,   // 无扩展 Card 时传 {}
    std::shared_ptr<AgentExecutor> agentExecutor,
    std::shared_ptr<TaskStore> taskStore = nullptr);
```

未提供 `taskStore` 时使用内置内存实现。HTTP 服务同时暴露 Agent Card（`/.well-known/agent-card.json`）与 JSON-RPC 端点。

### `AgentExecutor`

实现业务逻辑，由框架在收到 `message/send` / `message/stream` 时调用：

```cpp
virtual void Execute(std::shared_ptr<RequestContext> context,
                     std::shared_ptr<TaskUpdater> taskUpdater);

virtual void Cancel(std::shared_ptr<RequestContext> context,
                    std::shared_ptr<TaskUpdater> taskUpdater);
```

可选重载 `Execute(..., const std::string& method)` 处理自定义 JSON-RPC 方法。

### `TaskUpdater`

在 `Execute` 内推进任务状态并返回结果：

| 方法 | 对应状态 / 行为 |
|------|----------------|
| `Submit(message)` | `SUBMITTED` |
| `StartWork(message)` | `WORKING` |
| `RequiresInput(message)` | `INPUT_REQUIRED` |
| `RequiresAuth(message)` | `AUTH_REQUIRED` |
| `Complete(message)` | `COMPLETED` |
| `Failed(message)` | `FAILED` |
| `Reject(message)` | `REJECTED` |
| `Cancel(message)` | `CANCELED` |
| `UpdateStatus(state, message, ...)` | 任意状态 |
| `AddArtifact(artifactParam)` | 追加产物 |
| `SendResponseMessage(message)` | 直接发送消息响应 |
| `NewAgentMessage(parts, metadata)` | 构造 Agent 侧 `Message` |

### `RequestContext`

| 方法 | 说明 |
|------|------|
| `GetMessage()` | 用户消息 |
| `GetUserInput(delimiter)` | 拼接文本 Part |
| `GetCurrentTask()` | 当前任务 |
| `GetTaskId()` / `GetContextId()` | ID |
| `GetConfiguration()` | 发送配置 |
| `GetRelatedTasks()` | 关联任务 |
| `AttachRelatedTask(task)` | 附加关联任务 |
| `GetCallContext()` | 服务端调用上下文 |

### `TaskStore`

自定义任务持久化时实现：

| 方法 | 说明 |
|------|------|
| `Save(task, context)` | 保存或更新 |
| `Get(taskId, context)` | 按 ID 查询 |
| `Delete(taskId, context)` | 删除 |

---

## 日志（`a2a_log.h`）

SDK 诊断日志的完整说明（级别、`A2A_LOG` / `A2A_LOG_CONCAT`、`SetLogCallback` 限制、与协议事件的区别）见 **[LOGGING.md](LOGGING.md)**。

---

## 异常（`error.h`）

命名空间：`A2A`

### 客户端异常

| 类型 | 说明 |
|------|------|
| `A2AClientException` | 客户端异常基类，含 `errorCode`、`message`；提供 `Make()`、`TryParse()` |
| `A2AClientHTTPError` | HTTP 非 2xx 响应，含 `statusCode` |
| `A2AClientJSONError` | JSON-RPC / A2A 协议错误（响应体中的 error） |
| `A2AClientTimeoutError` | 请求超时 |

`GetTask()`、`GetAgentCard()` 等返回 `std::future` 的方法在失败时通过 future 抛出 `A2AClientException` 及其子类。可用 `A2AClientException::TryParse(e, out)` 从 `std::exception` 还原为 `A2AError`。

### 服务端异常

| 类型 | 说明 |
|------|------|
| `A2AServerError` | 服务端错误基类，含 `statusCode`（映射 JSON-RPC / A2A 错误码） |
| `MethodNotImplementedError` | 方法未实现（`-32601`） |

`A2AServerError` 支持默认构造（internal error `-32603`）或指定 `A2AErrorCode` 构造。

### 错误处理对照

| 场景 | 处理方式 |
|------|----------|
| `SendMessage` / `Resubscribe` 回调 | 检查 `ClientEvent` 中的 `A2AError` 变体 |
| `future::get()` 调用 | `catch (const A2AClientException&)` 或其子类 |
| 服务端 `AgentExecutor` | 抛出 `A2AServerError` / `MethodNotImplementedError` |

---

## 典型调用流程

### 客户端

```
HttpCardResolverBuilder::Build → GetAgentCard
    → ClientFactory::Create(card, config)
    → Client::SendMessage(msg, nullptr, handler)
    → handler 处理 ClientEvent
    → Client::Close()
```

### 服务端

```
实现 AgentExecutor
    → 构造 AgentCard + HttpConfig
    → HttpServerBuilder::Build(...)
    → Server::Start()
    → （运行中）Execute 内通过 TaskUpdater 更新状态
    → Server::Stop()
```

完整可运行代码见 `examples/helloworld_client.cpp`、`examples/helloworld_server.cpp`、`examples/streaming_*.cpp`。
