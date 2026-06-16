# MCP Client API

## 创建客户端

### Streamable HTTP

```cpp
#include "mcp_client.h"
#include "mcp_type.h"

Mcp::ClientConfig config;
config.name = "MyClient";
config.version = "1.0.0";

Mcp::StreamableHttpClientConfig httpConfig;
httpConfig.endpoint = "http://127.0.0.1:8000/mcp";
httpConfig.timeout = std::chrono::milliseconds(30000);

auto client = Mcp::McpClientFactory::CreateStreamableHttpClient(
    config, httpConfig, nullptr /* 可选 AuthProvider */);
```

### stdio

```cpp
Mcp::StdioClientConfig stdioConfig;
stdioConfig.command = "/path/to/mcp-server";
stdioConfig.args = {"--stdio"};

auto client = Mcp::McpClientFactory::CreateStdioClient(config, stdioConfig);
```

## 生命周期

推荐顺序：

```
创建 → （注册回调）→ Initialize() → RPC 调用 → CloseGracefully()
```

```cpp
auto initFuture = client->Initialize();
auto result = initFuture.get();  // 阻塞等待握手完成

// ... ListTools / CallTool 等 ...

client->CloseGracefully();
```

- 所有 RPC 方法返回 `std::future<std::shared_ptr<T>>`，通过 `future.get()` 获取结果。
- 未调用 `Initialize()` 前调用 RPC 会抛出 `std::runtime_error`。
- 远端 JSON-RPC 错误在 `future.get()` 时以 `Mcp::MCPError` 抛出。

## 常用 RPC

| 方法 | 说明 |
|------|------|
| `Initialize()` | 握手，获取服务端能力与版本 |
| `SendPing()` | 连通性检查 |
| `ListTools(cursor)` | 列出工具（支持分页 cursor） |
| `CallTool(name, arguments, timeout, progressCallback)` | 调用工具；`arguments` 为 JSON 字符串 |
| `ListResources` / `ReadResource` | 资源列表与读取 |
| `SubscribeResource` / `UnsubscribeResource` | 资源订阅 |
| `ListPrompts` / `GetPrompt` | Prompt 列表与获取 |
| `ListResourcesTemplates()` | 资源模板列表 |
| `Complete(ref, arg, context)` | 补全建议 |
| `SetLoggingLevel(level)` | 设置服务端日志级别 |
| `GetServerCapabilities()` | 获取服务端能力 |
| `SendProgressNotification(...)` | 发送进度通知 |
| `SendRootsListChanged()` | 通知 roots 列表变更 |
| `CloseGracefully()` | 关闭连接 |

## 回调注册

以下回调影响 `Initialize` 时对外宣告的能力，**建议在 `Initialize()` 之前注册**：

| 方法 | MCP 协议 | 说明 |
|------|----------|------|
| `SetListRootsCallback` | `roots/list` | 客户端提供 roots 列表 |
| `SetSamplingCreateMessageCallback` | `sampling/createMessage` | 处理服务端发起的采样请求 |
| `SetLoggingCallback` | `notifications/message` | 接收服务端日志通知 |
| `SetElicitCallback` | `elicitation/create`（form） | 表单式征询 |
| `SetElicitUrlCallback` | `elicitation/create`（url） | URL 式征询 |

Sampling 示例见 `example/client_example/sampling_example/`。

## 鉴权

HTTP 客户端可传入 `std::shared_ptr<Mcp::AuthProvider>`：

```cpp
auto auth = std::make_shared<Mcp::BearerTokenProvider>("your-token");
auto client = Mcp::McpClientFactory::CreateStreamableHttpClient(config, httpConfig, auth);
```

## 配置要点

`StreamableHttpClientConfig`（`mcp_type.h`）：

| 字段 | 说明 |
|------|------|
| `endpoint` | MCP HTTP 端点 URL |
| `timeout` | HTTP 请求超时 |
| `sseTimeout` | SSE 流超时 |
| `headers` | 额外 HTTP 头 |
| `tlsConfig` | TLS 证书与校验选项 |

## 日志

- **SDK 内部日志**：`MCP_LOG` + `SetLogLevel` / `SetLogCallback`（见 `mcp_log.h`）
- **协议日志通知**：`SetLoggingCallback`（与 `MCP_LOG` 不同，见 [protocol-mapping.md](protocol-mapping.md)）

## 示例

- 完整流程：`example/client_example/tool_example/tool_example.cpp`
- 分页 ListTools、Progress、Complete：`tool_example.cpp`
- Auth：`tool_example.cpp`（`--auth`）

## 参见

- [server.md](server.md)
- [protocol-mapping.md](protocol-mapping.md)
- 头文件：`include/mcp/mcp_client.h`
