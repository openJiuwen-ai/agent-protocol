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

`AgentCard` 中 `supportedInterfaces` 应包含指向 `http://host:port/jsonrpc` 的 JSON-RPC 接口描述。

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
