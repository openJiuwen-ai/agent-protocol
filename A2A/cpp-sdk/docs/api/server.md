# A2A Server API

## 创建服务端

### HTTP Server

```cpp
#include "server/http_server_builder.h"
#include "server/agent_executor.h"
#include "types.h"

A2A::Server::HttpConfig httpConfig;
httpConfig.ip = "127.0.0.1";
httpConfig.port = 8888;
httpConfig.endpoint = "/jsonrpc";  // 默认

A2A::AgentCard agentCard{};
agentCard.name = "ExampleAgent";
agentCard.version = "1.0.0";
// ... 配置 capabilities、supportedInterfaces 等

auto executor = std::make_shared<MyAgentExecutor>();

auto server = A2A::Server::HttpServerBuilder::Build(
    httpConfig,
    agentCard,
    {},           // extendedAgentCard（可选）
    executor,
    nullptr       // taskStore（nullptr = 内存存储）
);
```

Agent Card 同时通过 HTTP 暴露：`GET /.well-known/agent-card.json`。

## 生命周期

```
创建 → 实现 AgentExecutor → Build() → Start() → （处理请求）→ Stop()
```

```cpp
int ret = server->Start();
if (ret != 0) {
    // 启动失败，检查 A2A_LOG 日志
    return 1;
}

// 阻塞运行，直到收到退出信号
server->Stop();
```

- `Start()` 成功返回 `0`，失败返回非 0（错误信息见日志）。
- `Start()` 可能抛出 `std::runtime_error`（如 transport bind 失败）。
- 注册与 handler 异常、JSON-RPC 错误映射见 [errors.md](../errors.md)。
- `Stop()` 后不应再处理新请求。

## 实现 AgentExecutor

```cpp
class MyAgentExecutor : public A2A::Server::AgentExecutor {
public:
    void Execute(std::shared_ptr<A2A::Server::RequestContext> context,
        std::shared_ptr<A2A::Server::TaskUpdater> taskUpdater) override
    {
        taskUpdater->StartWork();

        A2A::Message response;
        response.role = A2A::Role::AGENT;
        // ... 构造 response.parts

        taskUpdater->SendResponseMessage(response);
    }

    void Cancel(std::shared_ptr<A2A::Server::RequestContext> context,
        std::shared_ptr<A2A::Server::TaskUpdater> taskUpdater) override
    {
        taskUpdater->Cancel();
    }
};
```

- **同步处理**：在 `Execute` 内直接通过 `TaskUpdater` 推送状态与消息。
- **流式处理**：多次调用 `taskUpdater` 发布 `TaskStatusUpdateEvent` / `TaskArtifactUpdateEvent`。
- 不可恢复错误可抛出 `A2AServerError` 子类。

## TaskUpdater 常用方法

| 方法 | 说明 |
|------|------|
| `StartWork()` | 任务进入 WORKING 状态 |
| `SendResponseMessage(msg)` | 发送 Agent 回复消息 |
| `Cancel()` | 标记任务已取消 |
| 状态/产物更新 API | 流式场景下推送事件（见 `task_updater.h`） |

## 自定义 TaskStore

```cpp
auto store = std::make_shared<MyTaskStore>();
auto server = A2A::Server::HttpServerBuilder::Build(
    httpConfig, agentCard, {}, executor, store);
```

默认 `nullptr` 使用内存 `InMemoryTaskStore`。

## 配置要点

`HttpConfig`：

| 字段 | 说明 |
|------|------|
| `ip` | 监听 IP |
| `port` | 监听端口 |
| `endpoint` | JSON-RPC 路径，默认 `/jsonrpc` |
| `ioThreadNum` | I/O 事件循环线程数 |
| `connectionTimeoutSeconds` | 空闲连接回收阈值（秒，默认 300，`0` 禁用）：仅对**无 in-flight 请求**的连接生效，静默超过该时长即关闭，防止空闲/失联客户端累积泄漏 fd |
| `connectionSweepIntervalSeconds` | 回收扫描间隔（秒，默认 30），自动收敛为不超过已启用的超时值 |
| `requestTimeoutSeconds` | in-flight 请求预算（秒，默认 1800，`0` 禁用）：handler 尚未完成响应期间允许的连接静默上限，从最近一次收发（读入请求或写出 chunk）重新计时 |

`AgentCard` 中 `supportedInterfaces` 应包含指向 `http://host:port/jsonrpc` 的 JSON-RPC 接口描述。

## 连接回收与客户端超时的配套关系

服务端两档回收预算与客户端超时是**相互独立**的旋钮。流式 agent 任务中"单步长时间无输出"（相邻 chunk 间隔很长）需要两端同时覆盖：

- 服务端 `requestTimeoutSeconds`（默认 1800s）：连接静默超过该值，服务端主动关闭连接；
- 客户端流式空闲超时 `streamingIdleTimeoutSeconds`（默认 60s，上限 1800s，见 [client.md - 超时语义](client.md#超时语义)）：静默超过该值，客户端主动断流。

只调大一端时，另一端仍会**先**切断流：客户端表现为 `A2A_TRANSPORT_EXCEPTION`（如 `Server returned nothing (no headers, no data)`）或 `stream-idle` 超时——恰是回收机制要消除的误报场景。两端默认值均为 1800s（与客户端超时上限对齐），默认配置下不会出现单侧提前切断；需要支持超过 1800s 的单步静默时，除调大本端 `requestTimeoutSeconds` 外，还必须搭配超时上限更高的客户端（本 SDK 客户端的 1800s 上限不可配置）。

## 流式中途的服务端自生成错误

chunked 流式响应的头已发出后，服务端自身产生的错误（路由 handler 同步抛异常导致的 500、同连接后续请求解析失败的 400/404 等）无法再作为带状态码的 HTTP 响应投递——HTTP/1.1 不允许在流式 body 中途插入第二个响应。此时 SDK 只写出终止 chunk 干净地结束当前流，并记录 ERROR 日志（`cannot deliver generated response on a started stream`）；客户端看到的是**无错误信息的正常截断**，需查服务端日志定位。handler/executor 层错误不受影响，仍按原路径返回（流式经 `event: error` 事件、非流式经 JSON-RPC error，见 [errors.md](../errors.md)）。

## 日志

- **SDK 诊断日志**：`A2A_LOG`（见 [logging.md](../logging.md)）
- **对外错误**：通过 JSON-RPC error 或流式 `A2AError` 事件返回，非 SDK 日志

## 示例

- 非流式 Server：`examples/helloworld_server.cpp`
- 流式 Server：`examples/streaming_server.cpp`
- 冒烟脚本：`scripts/run_example.sh`

## 参见

- [client.md](client.md)
- [protocol-mapping.md](protocol-mapping.md)
- 头文件：`include/server/http_server_builder.h`
