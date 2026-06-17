# A2A C++ SDK 日志说明

## 两套日志体系

SDK 中存在 **SDK 诊断日志** 与 **协议运行时数据** 两类机制，彼此不自动联动：

| 体系 | API | 输出目标 | 用途 |
|------|-----|----------|------|
| **SDK 诊断日志** | `A2A_LOG`、`SetLogLevel`、`SetLogCallback` | 默认 **stdout**（可自定义） | SDK 与应用程序内部调试 |
| **协议运行时数据** | `ClientEvent`、`TaskStatusUpdateEvent`、`A2AError` 等 | JSON-RPC 响应 / SSE 流 | 任务状态、业务消息、对外错误 |

A2A 协议当前 **未定义** 独立的协议级日志推送通道。协议方法对照见 [api/protocol-mapping.md](api/protocol-mapping.md#运行时信息-vs-诊断日志)。

## SDK 诊断日志（`a2a_log.h`）

### 基本用法

```cpp
#include "a2a_log.h"

A2A::Log::SetLogLevel(A2A::Log::A2A_LOG_LEVEL::INFO);  // 默认 INFO

A2A_LOG(A2A::Log::A2A_LOG_LEVEL::INFO,
    std::string("server started on port ") + std::to_string(port));
```

`A2A_LOG` 第一个参数为级别，第二个参数为 **已拼接好的 `std::string`**（不支持 `printf` 风格占位符）。

### 级别

| 常量 | 数值 | 说明 |
|------|------|------|
| `A2A_LOG_LEVEL::DEBUG` | 3 | 调试 |
| `A2A_LOG_LEVEL::INFO` | 4 | 信息（默认阈值） |
| `A2A_LOG_LEVEL::WARN` | 5 | 警告 |
| `A2A_LOG_LEVEL::ERROR` | 6 | 错误 |
| `A2A_LOG_LEVEL::FATAL` | 7 | 致命 |

`SetLogLevel` 设置全局阈值；**低于阈值的日志在 `A2A_LOG` 入口即被过滤**，自定义 callback 收到的也是过滤后的消息。默认 sink `A2aPrintfImpl` 在输出前也会再次检查级别。

无效级别（小于 `DEBUG` 或大于 `FATAL`）调用 `SetLogLevel` 会返回 `-1`，级别保持不变。

### 输出格式

通过 `A2A_LOG` 输出的每行前缀格式为：

```text
[YYYY-MM-DD HH:MM:SS.mmm] [tid] [LEVEL] file.cpp::Function:[line] message
```

其中 `LEVEL` 为 `DEBUG` / `INFO` / `WARN` / `ERROR` / `FATAL` 之一；`tid` 为 Linux 线程 ID（`syscall(SYS_gettid)`）。级别名由 SDK 自动写入；`A2aLogCallback` 的第一个参数仍为枚举值 `A2A_LOG_LEVEL`。

### 默认输出：stdout

默认回调 `A2aPrintfImpl` 通过 `printf` 写入 **stdout**（末尾自动换行）。

> **注意**：若应用将 **stdout** 用于其他用途（如管道输出、与协议数据混写），建议在进程启动早期通过 `SetLogCallback` 将日志重定向到 **stderr** 或文件：

```cpp
void StderrLogCallback(A2A::Log::A2A_LOG_LEVEL /*level*/, std::string message) {
    fprintf(stderr, "%s\n", message.c_str());
}

A2A::Log::SetLogCallback(StderrLogCallback);
```

A2A 传输基于 HTTP + JSON-RPC，通常不受 stdout 日志影响；上述注意主要适用于自定义集成场景。

### 自定义回调

```cpp
void MyLogCallback(A2A::Log::A2A_LOG_LEVEL level, std::string message) {
    // level 已通过 SetLogLevel 过滤；此处无需再次判断
    my_logger.write(message);
}

A2A::Log::SetLogCallback(MyLogCallback);
```

行为说明：

- `SetLogCallback` **进程内仅可成功调用一次**；重复调用返回 `-1`，并向 stdout 打印提示 `log callback can only be set once`。
- 设置自定义回调后 **无法通过 API 恢复** 默认 `A2aPrintfImpl`；如需切换 sink，应在自定义回调内部分发。
- 若 `logCallback` 为 `nullptr`，`A2A_LOG` **不会输出**。

### 与常见日志库集成

将 `A2A_LOG` 桥接到 spdlog、glog 等时，在 `SetLogCallback` 注册的函数内转发 `message` 即可；级别映射需自行对照上表。

## 示例

| 场景 | 参考 |
|------|------|
| 级别设置与过滤 | `tests/ut/log/test_logger.cpp` |
| `LogInternal` / `GetLogLevelName` | `tests/ut/log/test_a2a_log_internal.cpp` |
| SDK 内部用法 | `src/client/connection/libcurl_conn.cpp`、`src/server/net/tcp_listener.cpp` 等 |
| 协议事件对照 | [api/protocol-mapping.md](api/protocol-mapping.md) |

## 相关文档

- [client.md](api/client.md) — 客户端 API
- [server.md](api/server.md) — 服务端 API
- [testing.md](testing.md) — 测试说明
