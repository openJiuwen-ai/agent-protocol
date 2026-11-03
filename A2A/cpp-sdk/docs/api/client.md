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

`GetAgentCard` 可选参数：相对路径与发现超时（秒）：

```cpp
// 指定 10s 发现超时（默认 60s，0 = 默认值）
auto cardFuture = resolver->GetAgentCard(std::nullopt, 10);
```

`GetAllAgentCards(timeout)` 同样接受发现超时形参，语义与默认值相同。

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
| `timeoutSeconds` | 非流式请求总超时（秒，默认 300；<=0 用 SDK 默认值） |
| `streamingIdleTimeoutSeconds` | 流式空闲超时（秒，默认 60）：相邻数据到达的最大间隔；流式请求无总时长上限 |

## 超时语义

超时参数以**秒**为单位，`0` 表示使用默认值。所有超时共享 **1800s 上限**：配置或传入更大的值会按上限截断（记 WARN 日志），不会导致客户端创建失败。每个阶段独立控制：

| 阶段 | 配置入口 | 默认值 | 语义 |
|------|----------|--------|------|
| AgentCard 发现 | `A2ACardResolver::GetAgentCard(path, timeout)` / `GetAllAgentCards(timeout)` | 60s | 发现请求总超时 |
| TCP/TLS 连接 | 传输层内部（`LibcurlConn`） | 30s | 连接建立超时 |
| 非流式请求 | `ClientConfig::timeoutSeconds`，可被每次调用的 `timeout` 形参覆盖 | 300s | 请求总超时（含等待响应） |
| 流式空闲 | `ClientConfig::streamingIdleTimeoutSeconds`，可被 `SendMessage`/`Resubscribe` 的 `timeout` 形参覆盖 | 60s | 相邻数据到达的最大间隔，超时断开流 |

超时发生时通过 `A2AClientTimeoutError`（code `-32101`）抛出，错误消息携带阶段与已耗时，例如：

```
request timed out [stage=stream-idle, idle=60023ms, elapsed=60023ms]
request timed out [stage=waiting-response-body, elapsed=300012ms]
```

超时行为可用自带桩服务的示例直接验证（无需真实 server）：

```bash
./output/bin/timeout_client   # 退出码 = 失败场景数，0 = 全部通过
```

该示例内置"只 accept、永不回包"的 TCP 桩，覆盖发现超时、非流式 per-call/config 超时、流式空闲超时（config/per-call）共 5 个场景，见 `examples/timeout_client.cpp`。

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
