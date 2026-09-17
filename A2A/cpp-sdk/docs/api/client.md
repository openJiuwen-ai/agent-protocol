# A2A Client API

## 发现 Agent Card

### HTTP Card Resolver（推荐）

```cpp
#include "client/http_card_resolver_builder.h"

std::string baseUrl = "http://127.0.0.1:8888";
auto resolver = A2A::Client::HttpCardResolverBuilder::Build(baseUrl);

auto cardFuture = resolver->GetAgentCard();
A2A::AgentCard card = cardFuture.get();
```

默认路径为 `/.well-known/agent-card.json`（A2A 标准推荐方式）。

## 创建客户端

```cpp
#include "client/client_factory.h"
#include "client/client.h"

A2A::Client::ClientConfig config;
config.streaming = false;  // 非流式 message/send
config.polling = false;

auto client = A2A::Client::ClientFactory::Create(card, config);
```

也可传入自定义 `ClientTransport` 或 `ClientCallInterceptor` 列表，见 `client_factory.h`。

## 生命周期

推荐顺序：

```
解析 AgentCard → 创建 Client → （注册 Consumer / Interceptor）→ RPC 调用 → Close()
```

```cpp
// 发送消息（非流式）
std::mutex mtx;
std::condition_variable cv;
bool done = false;

client->SendMessage(msg, nullptr, [&](const A2A::Client::ClientEvent& ev,
    const A2A::AgentCard&) {
    // 处理 Message / A2AError / Task 更新
    std::lock_guard<std::mutex> lock(mtx);
    done = true;
    cv.notify_one();
}).get();

client->Close();
```

- 多数 RPC 返回 `std::future`；通过 `future.get()` 等待完成或捕获异常。
- 远端 JSON-RPC 错误在 `future.get()` 时以 `A2A::A2AClientException` 子类抛出。
- 完整错误语义见 [errors.md](../errors.md)。

## 常用 RPC

| 方法 | 说明 |
|------|------|
| `GetCard()` | 获取 Agent Card（JSON-RPC `GetAgentCard` 或经传输层） |
| `SendMessage(msg, ctx, handler, timeout)` | 发送消息（`SendMessage` / `SendStreamingMessage`） |
| `GetTask(params)` | 查询任务（`GetTask`） |
| `CancelTask(params)` | 取消任务（`CancelTask`） |
| `Resubscribe(params, ctx, handler)` | 重新订阅任务事件（`SubscribeToTask`） |
| `SetTaskPushNotificationConfig` 等 | 推送通知配置 CRUD |
| `AddEventConsumer(c)` | 注册全局事件消费者 |
| `AddRequestMiddleware(m)` | 注册请求拦截器 |
| `Close()` | 关闭客户端并释放资源 |

## 流式消息

```cpp
A2A::Client::ClientConfig config;
config.streaming = true;

auto client = A2A::Client::ClientFactory::Create(card, config);

client->SendMessage(msg, &ctx, [](const A2A::Client::ClientEvent& ev,
    const A2A::AgentCard& agentCard) {
    // 可能多次回调：Task + StatusUpdate / ArtifactUpdate
}, 0);
```

流式示例见 `examples/streaming_client.cpp`。

## 回调与事件

`ClientEvent` 类型：

```cpp
std::variant<Message, A2AError, std::pair<Task, UpdateEvent>>
```

| 组件 | 说明 |
|------|------|
| `Consumer` | 全局事件回调 |
| `ResponseHandler` | 单次 `SendMessage` / `Resubscribe` 的响应回调 |
| `ClientCallContext` | 每请求的 `state` / `headers` |

## 配置要点

`ClientConfig`（`client/client.h`）：

| 字段 | 说明 |
|------|------|
| `streaming` | 是否使用 `SendStreamingMessage` |
| `polling` | 非流式时是否轮询任务状态 |
| `supportedTransports` | 客户端支持的传输标签 |
| `acceptedOutputModes` | 接受的输出模式 |
| `pushNotificationConfigs` | 默认推送通知配置 |

## 日志

- **SDK 内部日志**：`A2A_LOG` + `SetLogLevel` / `SetLogCallback`（见 `a2a_log.h`）
- **协议数据**：`ClientEvent` 中的 `Message`、`A2AError`、任务更新（见 [protocol-mapping.md](protocol-mapping.md)）

## 示例

- 完整流程：`examples/helloworld_client.cpp`
- 流式：`examples/streaming_client.cpp`
- 冒烟脚本：`scripts/run_example.sh`

## 参见

- [server.md](server.md)
- [protocol-mapping.md](protocol-mapping.md)
- 头文件：`include/client/client.h`
