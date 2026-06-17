# MCP C++ SDK 日志说明

## 两套日志体系

SDK 中存在 **两套互不自动联动** 的日志机制：

| 体系 | API | 输出目标 | 用途 |
|------|-----|----------|------|
| **SDK 诊断日志** | `MCP_LOG`、`SetLogLevel`、`SetLogCallback` | 默认 **stdout**（可自定义） | SDK 与应用程序内部调试 |
| **MCP 协议日志** | `SetLoggingCallback`、`SendLogMessage`、`RegisterSetLoggingLevelHandler` | JSON-RPC `notifications/message` | 服务端向客户端推送的协议级日志 |

协议方法对照见 [protocol-mapping.md](api/protocol-mapping.md#两套日志区别)。

## SDK 诊断日志（`mcp_log.h`）

### 基本用法

```cpp
#include "mcp_log.h"

SetLogLevel(MCP_LOG_LEVEL_INFO);  // 默认 INFO

MCP_LOG(MCP_LOG_LEVEL_INFO, std::string("server started on port ") + std::to_string(port));
```

`MCP_LOG` 第一个参数为级别，第二个参数为 **已拼接好的 `std::string`**（不支持 `printf` 风格占位符）。

### 级别

| 常量 | 数值 | 说明 |
|------|------|------|
| `MCP_LOG_LEVEL_DEBUG` | 3 | 调试 |
| `MCP_LOG_LEVEL_INFO` | 4 | 信息（默认阈值） |
| `MCP_LOG_LEVEL_WARN` | 5 | 警告 |
| `MCP_LOG_LEVEL_ERROR` | 6 | 错误 |
| `MCP_LOG_LEVEL_FATAL` | 7 | 致命 |

`SetLogLevel` 设置全局阈值；**低于阈值的日志在 `MCP_LOG` 入口即被过滤**，自定义 callback 也会收到已过滤后的消息。

### 输出格式

通过 `MCP_LOG` 输出的每行前缀格式为：

```text
[YYYY-MM-DD HH:MM:SS.mmm] [tid] [LEVEL] file.cpp::Function:[line] message
```

其中 `LEVEL` 为 `DEBUG` / `INFO` / `WARN` / `ERROR` / `FATAL` 之一。级别名由 SDK 自动写入；`McpLogCallback` 的第一个参数仍为枚举值 `MCP_LOG_LEVEL`。

### 默认输出：stdout

默认实现 `McpPrintfImpl` 写入 **stdout**（`printf`）。

> **stdio 模式注意**：MCP stdio 传输同样使用 **stdout** 传输 JSON-RPC。stdio 场景下 **必须** 在启动时通过 `SetLogCallback` 将日志重定向到文件或 stderr，否则 SDK 日志会污染协议流：

```cpp
void StderrLogCallback(MCP_LOG_LEVEL /*level*/, std::string message) {
    fprintf(stderr, "%s\n", message.c_str());
}
SetLogCallback(StderrLogCallback);
```

HTTP 传输模式不受此限制。

### 自定义回调

```cpp
void MyLogCallback(MCP_LOG_LEVEL level, std::string message) {
    // level 已通过 SetLogLevel 过滤；此处无需再次判断
    my_logger.write(message);
}

SetLogCallback(MyLogCallback);
```

- 传入 `nullptr` 会恢复默认 `McpPrintfImpl`（stdout）。
- `SetLogCallback` 切换回调时 **不会** 额外打印状态信息。

### 与常见日志库集成

将 `MCP_LOG` 桥接到 spdlog、glog 等时，在 callback 内转发 `message` 即可；级别映射需自行对照上表。

## MCP 协议日志

### 客户端

```cpp
client->SetLoggingCallback([](const std::string& level,
                              const std::string& data,
                              const std::string& logger) {
    // 处理服务端推送的 notifications/message
});

client->SetLoggingLevel(Mcp::LoggingLevel::Info);
```

若未设置 `SetLoggingCallback`，收到 `notifications/message` 时 SDK 会以 `MCP_LOG(ERROR)` 记录。

### 服务端

```cpp
server->RegisterSetLoggingLevelHandler([](const std::string& level) {
    // 响应客户端 logging/setLevel；可按需映射到 SetLogLevel
});

session->SendLogMessage("info", "tool execution started", "my-server");
```

`RegisterSetLoggingLevelHandler` **不会** 自动修改 `SetLogLevel`；若需联动，在 handler 内显式调用。

## 示例

| 场景 | 参考 |
|------|------|
| HTTP Server 写文件日志 | `example/server_example/server_example.cpp`（`FileLogCallback`） |
| 协议日志对照 | [api/protocol-mapping.md](api/protocol-mapping.md) |
| 单元测试 | `tests/ut/log/test_log.cpp` |

## 相关文档

- [client.md](api/client.md) — 客户端 API
- [server.md](api/server.md) — 服务端 API
- [testing.md](testing.md) — 测试说明
