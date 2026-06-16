# A2A C++ SDK 日志说明

## 日志体系

A2A C++ SDK 提供 **SDK 诊断日志**（`a2a_log.h`），用于 SDK 内部与应用程序的调试输出。

A2A 协议当前 **未定义** 独立的协议级日志推送通道。任务状态、错误等运行时信息通过 **A2A 协议消息与流式事件**（`Message`、`TaskStatusUpdateEvent`、`A2AError` 等）传递，而非单独的日志通道。

| 体系 | API | 输出目标 | 用途 |
|------|-----|----------|------|
| **SDK 诊断日志** | `A2A_LOG`、`A2A_LOG_CONCAT`、`SetLogLevel`、`SetLogCallback` | 默认 **stdout**（可自定义） | SDK 与应用程序内部调试 |

## SDK 诊断日志（`a2a_log.h`）

### 基本用法

```cpp
#include "a2a_log.h"

A2A::Log::SetLogLevel(A2A::Log::A2A_LOG_LEVEL::INFO);  // 默认 INFO

// 推荐：支持 << 拼接
A2A_LOG_CONCAT(A2A::Log::A2A_LOG_LEVEL::INFO,
    "server started on port " << port);

// 遗留写法：第二个参数为完整字符串（不支持 printf 占位符）
A2A_LOG(A2A::Log::A2A_LOG_LEVEL::INFO, "hello from client");
```

**推荐优先使用 `A2A_LOG_CONCAT`**，可在宏内用 `<<` 拼接字符串；`A2A_LOG` 为遗留接口，消息体按 **原样字符串** 记录，不会解析 `%d` 等格式占位符。

### 级别

| 常量 | 数值 | 说明 |
|------|------|------|
| `A2A_LOG_LEVEL::DEBUG` | 3 | 调试 |
| `A2A_LOG_LEVEL::INFO` | 4 | 信息（默认阈值） |
| `A2A_LOG_LEVEL::WARN` | 5 | 警告 |
| `A2A_LOG_LEVEL::ERROR` | 6 | 错误 |
| `A2A_LOG_LEVEL::FATAL` | 7 | 致命 |

`SetLogLevel` 设置全局阈值；**低于阈值的日志在宏入口即被过滤**，自定义 callback 收到的也是过滤后的消息。默认 sink `A2aPrintfImpl` 在输出前也会再次检查级别。

无效级别（小于 `DEBUG` 或大于 `FATAL`）调用 `SetLogLevel` 会返回 `-1`，级别保持不变。

### `A2A_LOG` 与 `A2A_LOG_CONCAT` 格式差异

两者最终都调用 `logCallback`，但格式化路径不同：

| 宏 | 格式化 | 源文件字段 |
|----|--------|------------|
| `A2A_LOG` | 经 `LogInternal`，含时间戳与 tid | **basename**（去掉路径） |
| `A2A_LOG_CONCAT` | 宏内拼接时间戳与 tid | 完整 `__FILE__` |

推荐新代码使用 `A2A_LOG_CONCAT`；`A2A_LOG` 保留用于与现有 SDK 内部调用兼容。

### 日志行格式

每条日志经 `LogInternal` / `A2A_LOG_CONCAT` 格式化后，大致为：

```text
[YYYY-MM-DD HH:MM:SS.mmm] [tid] filename::Function:[line] message
```

其中 `tid` 为 Linux 线程 ID（`syscall(SYS_gettid)`）。

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
- 若 `logCallback` 为 `nullptr`，`A2A_LOG` / `A2A_LOG_CONCAT` **不会输出**（`A2A_LOG_CONCAT` 在宏内检查 `logCallback`）。

### 与常见日志库集成

将 `A2A_LOG_CONCAT` 桥接到 spdlog、glog 等时，在 `SetLogCallback` 注册的函数内转发 `message` 即可；级别映射需自行对照上表。

示例：

```cpp
A2A::Log::SetLogCallback([](A2A::Log::A2A_LOG_LEVEL level, std::string message) {
    switch (level) {
        case A2A::Log::A2A_LOG_LEVEL::DEBUG: spdlog::debug("{}", message); break;
        case A2A::Log::A2A_LOG_LEVEL::INFO:  spdlog::info("{}", message); break;
        case A2A::Log::A2A_LOG_LEVEL::WARN:  spdlog::warn("{}", message); break;
        case A2A::Log::A2A_LOG_LEVEL::ERROR: spdlog::error("{}", message); break;
        case A2A::Log::A2A_LOG_LEVEL::FATAL: spdlog::critical("{}", message); break;
    }
});
```

### 运行时信息 vs 诊断日志

| 类型 | 机制 | 场景 |
|------|------|------|
| **诊断日志** | `A2A_LOG` / `A2A_LOG_CONCAT` | 连接失败、内部状态、调试跟踪 |
| **协议数据** | `ClientEvent`、`TaskStatusUpdateEvent`、`A2AError` 等 | 智能体业务消息、任务状态、对外错误 |

业务侧应通过 Client/Server API 处理协议事件；不要将协议回调中的信息再用 `A2A_LOG` 重复记录，除非用于本地调试。

## 示例

| 场景 | 参考 |
|------|------|
| 级别设置与过滤 | `tests/ut/log/test_logger.cpp` |
| `LogInternal` / 默认 sink | `tests/ut/log/test_a2a_log_internal.cpp` |
| SDK 内部用法 | `src/client/connection/libcurl_conn.cpp`、`src/server/net/tcp_listener.cpp` 等 |

## 相关文档

- [API.md](API.md) — 公开头文件索引（含 `a2a_log.h`）
- [TESTING.md](TESTING.md) — 测试说明
- [README.md](../README.md) — 快速入门
