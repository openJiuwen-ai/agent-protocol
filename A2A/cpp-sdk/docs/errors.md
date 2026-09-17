# A2A C++ SDK 错误处理说明

本文说明 SDK 中 **错误码、异常类型与返回值** 的语义及推荐处理方式。公开类型定义见 `include/error.h`、`include/types.h`。

## 错误表达方式概览

SDK 同时使用多种机制表达失败，**彼此不自动转换**：

| 表达方式 | 典型场景 | 如何感知 |
|----------|----------|----------|
| `A2AClientException` 及子类 | Client 传输层、JSON-RPC、A2A 协议错误 | RPC：`future.get()` 抛异常 |
| `A2AServerError` 及子类 | Server handler 内不可恢复错误 | 映射为 JSON-RPC error 响应 |
| `A2AError`（结构体） | 流式 `ClientEvent` 中的协议错误 | `ResponseHandler` / `Consumer` 回调 |
| `int`（`Server::Start()`） | Server 启动失败 | 返回非 0，详情见日志 |

## 公开类型：`A2AErrorCode` 与 `A2AError`

### JSON-RPC 标准错误码

```cpp
enum class A2AErrorCode : int {
    JSONRPC_PARSE_ERROR      = -32700,
    JSONRPC_INVALID_REQUEST  = -32600,
    JSONRPC_METHOD_NOT_FOUND = -32601,
    JSONRPC_INVALID_PARAMS   = -32602,
    JSONRPC_INTERNAL_ERROR   = -32603,
    // A2A 扩展码 -32001 … -32009、Client 码 -32101 … 等
};
```

`A2AError` 结构体携带 `code`、`message` 等字段；`types.h` 中提供 `TaskNotFoundError`、`InvalidParamsError` 等便捷子类型。

### Client 异常层次

| 类型 | 说明 |
|------|------|
| `A2AClientException` | 基类；`errorCode` + `message` |
| `A2AClientHTTPError` | 非 2xx HTTP 响应；`statusCode` 为 HTTP 状态码 |
| `A2AClientJSONError` | JSON-RPC / A2A 协议错误；`errorCode` 为 wire code |
| `A2AClientTimeoutError` | 请求超时 |

辅助 API：

| 方法 | 说明 |
|------|------|
| `A2AClientException::Make(code, msg)` | 构造 `exception_ptr` 供 future 使用 |
| `A2AClientException::TryParse(e, out)` | 从 `std::exception` 解析为 `A2AError` |

> **注意**：`A2AClientException` 继承 `std::runtime_error`。捕获时建议 **先** `catch (const A2A::A2AClientJSONError&)` / 子类，再 `catch (const A2A::A2AClientException&)`，最后 `catch (const std::runtime_error&)`。

## Client 侧

### RPC 与 `future.get()`

`SendMessage`、`GetTask`、`CancelTask`、`GetCard` 等返回 `std::future`。在 `future.get()` 时可能抛出：

| 异常 | 原因 |
|------|------|
| `A2AClientJSONError` | 对端返回 JSON-RPC / A2A error |
| `A2AClientHTTPError` | HTTP 传输失败（非 2xx） |
| `A2AClientTimeoutError` | 请求超时 |
| `A2AClientException` | 其他 Client 错误 |
| `std::runtime_error` | SDK 本地状态错误等 |

**推荐捕获顺序：**

```cpp
try {
    auto task = client->GetTask(params).get();
} catch (const A2A::A2AClientJSONError& e) {
    // 协议错误：读 e.errorCode、e.message
} catch (const A2A::A2AClientHTTPError& e) {
    // HTTP 错误：读 e.statusCode
} catch (const A2A::A2AClientException& e) {
    // 其他 Client 错误
} catch (const std::runtime_error& e) {
    // SDK 本地错误
}
```

### 流式 `SendMessage` 与 `ClientEvent`

`SendMessage` 的 `ResponseHandler` 可能收到 `ClientEvent`：

```cpp
std::variant<Message, A2AError, std::pair<Task, UpdateEvent>>
```

- **非流式**：通常收到 `Message` 或 `A2AError`。
- **流式**：可能多次收到 `std::pair<Task, UpdateEvent>`（状态/产物更新）。

协议层错误可能 **同时** 通过 handler 中的 `A2AError` 与 `future.get()` 异常表达，调用方应处理两种路径。

### 常见 Client 场景

| 场景 | 表现 | 处理建议 |
|------|------|----------|
| 远端方法不存在 | `A2AClientJSONError`，code 多为 `-32601` | 检查 Agent Card URL 与 Server 版本 |
| 任务不存在 | code 常为 `TASK_NOT_FOUND`（-32001） | 校验 taskId |
| HTTP 连接失败 | `A2AClientHTTPError` | 检查网络与 `LD_LIBRARY_PATH` |
| Card 解析失败 | `A2AClientJSONError` | 检查 `/.well-known/agent-card.json` |

## Server 侧

### 启动与生命周期

| 场景 | 表现 | 处理建议 |
|------|------|----------|
| `Start()` 失败 | 返回非 0 | 查 `A2A_LOG` 日志（端口占用、bind 失败等） |
| `HttpServerBuilder::Build` 配置非法 | `std::runtime_error` | 修正 `HttpConfig` / AgentCard |
| `AgentExecutor::Execute` 抛 `A2AServerError` | 映射为 JSON-RPC error | 使用合适的 `statusCode` |

`Start()` **一般不抛异常**；失败原因需通过日志排查。

### 请求处理中的 JSON-RPC 错误

Server 收到请求后，常见映射：

| 情况 | 典型 `A2AErrorCode` |
|------|---------------------|
| 未知 method | `JSONRPC_METHOD_NOT_FOUND`（-32601） |
| params 类型/字段错误 | `JSONRPC_INVALID_PARAMS`（-32602） |
| 任务不存在 | `TASK_NOT_FOUND`（-32001） |
| 方法未实现 | `MethodNotImplementedError` → -32601 |
| 内部未预期异常 | `JSONRPC_INTERNAL_ERROR`（-32603） |

### AgentExecutor 业务错误

在 `Execute` / `Cancel` 中通过 `TaskUpdater` 发布状态与消息是推荐路径；不可恢复错误可抛出 `A2AServerError` 或其子类，由 SDK 转为 JSON-RPC error。

## 错误传播路径（Client RPC）

```
远端 JSON-RPC error
    → 传输层解析
    → promise::set_exception(A2AClientJSONError)
    → future.get() 抛出 A2AClientJSONError
```

传输失败等本地路径可能抛出 `A2AClientHTTPError` 或 `A2AClientTimeoutError`。

## 与协议事件的区别

| 概念 | 相关 API | 说明 |
|------|----------|------|
| 错误 / 异常 | `A2AClientException`、`A2AError` | 调用失败或任务错误 |
| 任务状态 / 消息 | `ClientEvent`、`TaskStatusUpdateEvent` | 正常协议数据流，**不是** SDK 诊断日志 |

详见 [logging.md](logging.md)。

## 示例与测试参考

| 场景 | 参考 |
|------|------|
| Client 异常解析 | `tests/ut/shared/test_error.cpp` |
| JSON-RPC 错误响应 | `tests/ut/server/test_default_request_handler.cpp` |
| HTTP 错误 | `tests/ut/client/test_http_card_resolver.cpp` |
| 示例中的错误处理 | `examples/helloworld_client.cpp` |

## 相关文档

- [client.md](api/client.md) — Client API
- [server.md](api/server.md) — Server 生命周期
- [protocol-mapping.md](api/protocol-mapping.md) — 协议方法对照
- [error.h](../include/error.h) — 类型定义
