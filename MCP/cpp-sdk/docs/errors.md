# MCP C++ SDK 错误处理说明

本文说明 SDK 中 **错误码、异常类型与返回值** 的语义及推荐处理方式。公开类型定义见 `include/mcp/mcp_error.h`。

## 错误表达方式概览

SDK 同时使用多种机制表达失败，**彼此不自动转换**：

| 表达方式 | 典型场景 | 如何感知 |
|----------|----------|----------|
| `Mcp::MCPError` | 通过 MCP/JSON-RPC 连接收到的协议层错误 | Client RPC：`future.get()` 抛异常 |
| `std::runtime_error` | SDK 状态错误、类型不匹配、资源未找到等 | 同步 API 或 `future.get()` |
| `std::invalid_argument` | 本地参数非法（空 name、null handler、非法 config） | 构造或注册阶段立即抛出 |
| `CallToolResult.isError` | Tool handler **业务层**失败 | 正常返回 Result，**不抛** JSON-RPC 异常 |
| `bool`（`Run()`） | Server 启动失败 | 返回 `false`，详情见日志 |

## 公开类型：`JsonRpcErrorCode` 与 `MCPError`

### JSON-RPC 标准错误码

```cpp
enum class JsonRpcErrorCode : int {
    PARSE_ERROR     = -32700,
    INVALID_REQUEST = -32600,
    METHOD_NOT_FOUND = -32601,
    INVALID_PARAMS  = -32602,
    INTERNAL_ERROR  = -32603,
    SERVER_ERROR    = -32000  // -32000 ~ -32099 保留给服务端自定义
};
```

`MCPError` 携带完整 JSON-RPC error object 字段：

| 方法 | 说明 |
|------|------|
| `code()` | 整数错误码 |
| `codeEnum()` | 映射为已知 `JsonRpcErrorCode`；未知码（含 -32001…-32099）返回 `std::nullopt` |
| `message()` | 错误消息 |
| `data()` | 可选附加数据（字符串） |
| `what()` | 同 `message()`（继承自 `std::runtime_error`） |

> **注意**：`MCPError` 继承 `std::runtime_error`。捕获时必须 **先** `catch (Mcp::MCPError&)`，再 `catch (std::runtime_error&)`，否则协议错误会被泛化捕获。

## Client 侧

### RPC 与 `future.get()`

所有 Client RPC（`Initialize`、`ListTools`、`CallTool` 等）返回 `std::future<std::shared_ptr<T>>`。在 `future.get()` 时可能抛出：

| 异常 | 原因 |
|------|------|
| `Mcp::MCPError` | 对端返回 JSON-RPC error object |
| `std::runtime_error` | 未初始化、结果类型不匹配、传输层本地错误等 |

**推荐捕获顺序：**

```cpp
try {
    auto result = client->CallTool("echo", R"({"msg":"hi"})").get();
    if (result->isError) {
        // 业务失败：Tool 返回 isError=true，协议层仍为成功响应
        // 处理 result->content
    }
} catch (const Mcp::MCPError& e) {
    // 协议错误：读 e.code()、e.message()、e.data()
} catch (const std::runtime_error& e) {
    // SDK 本地错误或类型不匹配
}
```

### 常见 Client 场景

| 场景 | 表现 | 处理建议 |
|------|------|----------|
| 未调用 `Initialize()` 就调 RPC | `std::runtime_error("client is not initialized.")` | 先完成握手 |
| 重复 `Initialize()` | `std::runtime_error("client is initialized.")` | 检查生命周期 |
| 远端方法不存在 / 参数非法 | `MCPError`，code 多为 `METHOD_NOT_FOUND` / `INVALID_PARAMS` | 按 `code()` 分支 |
| Tool 执行失败（handler 抛异常） | 通常 **不** 抛 `MCPError`；返回 `CallToolResult` 且 `isError == true` | 检查 Result |
| Tool 不存在 / 参数解析失败（Server 映射为 JSON-RPC error） | `MCPError`，code 常为 `INVALID_PARAMS` | 修正请求或 Tool 名 |
| `arguments` 非法 JSON 字符串 | 可能在构造请求时抛 `nlohmann::json` 相关异常 | 调用前校验 JSON |
| HTTP 未编译进 SDK 时创建 HTTP Client | `std::runtime_error("HTTP client is not enabled in this build")` | 使用 stdio 或开启 `MCP_WITH_HTTP` |

### `CallTool` 的双路径（重要）

`CallTool` 可能以两种方式表示失败，调用方需 **同时** 考虑：

1. **协议层失败** → `future.get()` 抛出 `MCPError`（例如 Server 在 `tools/call` 层返回 JSON-RPC error）
2. **业务层失败** → `future.get()` 成功，但 `result->isError == true`（Tool handler 异常时 Server 将其转为 Result）

二者语义不同：**前者是 RPC 调用本身失败，后者是 RPC 成功但 Tool 报告错误**。

### Client 向 Server 发起的请求（回调）

`SetListRootsCallback`、`SetSamplingCreateMessageCallback` 等由 Server 反向发起的请求，错误传播路径与 Client 主动 RPC 类似；具体行为见对应 API 的 `@throw` 说明。

### `CallTool` 的 `timeout` 参数

API 签名包含 `int timeout`，**当前实现未使用该参数**。超时控制应依赖传输层配置（如 `StreamableHttpClientConfig::timeout`），或自行在应用层对 `future.wait_for` 包装。后续版本可能实现或移除此参数。

## Server 侧

### 启动与生命周期

| 场景 | 表现 | 处理建议 |
|------|------|----------|
| `Run()` 时状态非 INIT | 返回 `false`，日志说明当前状态 | 勿重复 `Run()` |
| `InitializeServerManager()` 失败 | 返回 `false` | 查日志（endpoint、端口等） |
| `Stop()` 后调用 `AddTool` 等 | `std::runtime_error("Cannot perform operation: server has been stopped")` | 勿在 STOP 后注册 |
| 创建时 config 非法 | `std::invalid_argument` | 修正 `ServerConfig` / transport config |

`Run()` **不抛异常**，失败原因需通过 `MCP_LOG` 日志排查。

### 注册 API（本地错误）

| API | 常见异常 |
|-----|----------|
| `AddTool` / `AddPrompt` / `AddResource` | `invalid_argument`（空 name、null handler）；`runtime_error`（重名且未 overwrite） |
| `RemoveTool` 等 | `invalid_argument`（空 key）；`runtime_error`（不存在） |

这些是 **应用编程错误**，不会转为 JSON-RPC 响应。

### 请求处理中的 JSON-RPC 错误

Server 收到 MCP 请求后，handler 内通过 `SendErrorResponse` 返回 JSON-RPC error，常见映射：

| 情况 | 典型 `JsonRpcErrorCode` |
|------|-------------------------|
| 未知 method | `METHOD_NOT_FOUND` |
| params 类型/字段错误 | `INVALID_PARAMS` |
| `tools/call` 捕获的异常（含 Tool 不存在、参数解析失败） | 当前多为 `INVALID_PARAMS` |
| 未注册 handler（如 completion） | `SERVER_ERROR` |
| 内部未预期异常 | `SERVER_ERROR` 或 `INTERNAL_ERROR` |

### Tool 业务错误 vs 协议错误

在 `ToolManager::CallTool` 中，Tool handler 抛出的 `std::exception` 会被捕获并转为 **`CallToolResult.isError = true`**，作为 **正常 JSON-RPC result** 返回，而非 error object。

若希望 Client 收到 `MCPError`，需在更高层（如自定义 middleware）显式 `SendErrorResponse`，而不是依赖 handler 抛异常。

### Server 向 Client 发起的请求

`McpServerSession::SamplingCreateMessage`、`ListRoots` 等返回 `std::future`。对端 JSON-RPC error 经 `ErrorResult` 回调；**当前 ServerSession 实现可能以 `std::runtime_error`（如 "result type mismatch"） surfaced**，而非 `MCPError`。调用方应同时准备捕获 `runtime_error` 并检查 message，或查阅最新版本行为。

## 错误传播路径（Client RPC）

```
远端 JSON-RPC error
    → 解码为 ErrorResult
    → ClientSession completion 中 throw MCPError
    → promise::set_exception
    → future.get() 抛出 MCPError
```

传输失败等本地路径可能直接构造 `ErrorResult(INTERNAL_ERROR)` 或通过 `runtime_error` 传递。

## 与协议日志的区别

| 概念 | 相关 API | 说明 |
|------|----------|------|
| 错误 / 异常 | `MCPError`、`CallToolResult.isError` | 调用失败或业务失败 |
| 协议日志 | `SetLoggingCallback`、`notifications/message` | 服务端推送的日志通知，**不是**错误码 |

详见 [logging.md](logging.md)（若已提供）。

## 示例与测试参考

| 场景 | 参考 |
|------|------|
| Client `MCPError` | `tests/ut/client/client_test.cpp` |
| Tool `isError` | `tests/ut/server/tool_manager_test.cpp` |
| Server `SendErrorResponse` | `tests/ut/server/mcp_server_implement_test.cpp` |
| JSON-RPC 编解码 | `tests/ut/shared/jsonrpc_test.cpp` |
| 示例中的 catch | `example/server_example/server_example.cpp` |

## 相关文档

- [client.md](api/client.md) — Client API 与 `@throw` 摘要
- [server.md](api/server.md) — Server 生命周期
- [protocol-mapping.md](api/protocol-mapping.md) — 协议方法对照
- [mcp_error.h](../include/mcp/mcp_error.h) — 类型定义
