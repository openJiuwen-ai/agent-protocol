# MCP Server API

## 创建服务端

### Streamable HTTP

```cpp
#include "mcp_server.h"
#include "mcp_type.h"

Mcp::ServerConfig config;
config.name = "MyServer";
config.version = "1.0.0";
config.workerThreads = 2;

Mcp::StreamableHttpServerConfig transportConfig;
transportConfig.endpoint = "http://127.0.0.1:8000/mcp";
transportConfig.ioThreads = 2;
transportConfig.stateless = false;

auto server = Mcp::McpServerFactory::CreateStreamableHttpServer(config, transportConfig);
```

### stdio

```cpp
auto server = Mcp::McpServerFactory::CreateStdioServer(config);
```

## 生命周期

```
创建 → 注册 Tool/Prompt/Resource → Run() → （处理请求）→ Stop()
```

```cpp
// 注册 handler（见下文）
server->AddTool("echo", toolFunc, {});

if (!server->Run()) {
    // 启动失败，检查日志
    return 1;
}

// 阻塞运行，直到 Stop() 或进程退出
server->Stop();
```

- `Run()` 成功返回 `true`，失败返回 `false`（错误信息见日志）。
- 注册 API 异常、JSON-RPC 错误映射与 Tool `isError` 语义见 [errors.md](../errors.md)。
- `Stop()` 后不应再调用 `AddTool` 等注册接口。

## 注册 Tool

```cpp
server->AddTool("echo", [](const Mcp::ServerContext& ctx,
                           const std::string& name,
                           const std::string& arguments) -> Mcp::ToolReturn {
    Mcp::CallToolResult result;
    Mcp::TextContent text;
    text.text = "Echo: " + arguments;
    result.content.push_back(text);
    return result;
}, {});
```

- **同步 handler**：直接返回 `CallToolResult` 或 JSON 字符串。
- **异步 handler**：返回 `void`，通过 `ctx.responseCallback(result)` 回传结果。
- 可选参数使用 `AddToolOptionalParams`（title、description、inputSchema 等）。

## 注册 Prompt / Resource

```cpp
server->AddPrompt("greeting", renderFunc, {});

server->AddResource("http://example.com/res", "MyResource", readFunc, {});
server->AddResourceTemplate("http://example.com/t/{id}", "Template", {});
```

移除：`RemoveTool` / `RemovePrompt` / `RemoveResource` / `RemoveResourceTemplate`。

## 会话能力（McpServerSession）

在 Tool/Prompt/Resource handler 的 `ServerContext::session` 上可调用：

| 方法 | 说明 |
|------|------|
| `ListRoots()` | 向客户端请求 roots 列表 |
| `SamplingCreateMessage(params)` | 向客户端发起采样 |
| `SendProgressNotification(...)` | 发送进度通知 |
| `SendToolListChangedNotification()` 等 | 列表变更通知 |

## 其他注册

| 方法 | 说明 |
|------|------|
| `RegisterSetLoggingLevelHandler` | 处理 `logging/setLevel` |
| `AddCompletion` | 处理 `completion/complete` |

## 配置要点

`ServerConfig`：

| 字段 | 说明 |
|------|------|
| `workerThreads` | 工作线程数 |
| `toolsPageSize` / `resourcesPageSize` | 分页大小 |
| `capabilities` | 对外宣告的服务端能力 |

`StreamableHttpServerConfig`：

| 字段 | 说明 |
|------|------|
| `endpoint` | 监听 URL |
| `stateless` | 无状态模式（限制部分会话 API） |
| `isJsonResponseEnabled` | JSON 响应模式 |
| `ioThreads` | I/O 线程数 |
| `tlsConfig` | TLS |
| `authenticator` / `authorizer` | 鉴权（见 `mcp_auth.h`） |

## 鉴权

```cpp
auto verifier = std::make_shared<MyTokenVerifier>();
auto authenticator = std::make_shared<Mcp::BearerTokenAuthenticator>(verifier);
auto authorizer = std::make_shared<Mcp::ScopeBasedAuthorizer>("read write");

transportConfig.authenticator = authenticator;
transportConfig.authorizer = authorizer;
```

Auth 示例：启动 `example/server_example/ServerExample --auth`（端口 8001）。

## 示例

- 完整 Server：`example/server_example/server_example.cpp`
- 运行说明：[example/server_example/README.md](../../example/server_example/README.md)

## 参见

- [client.md](client.md)
- [protocol-mapping.md](protocol-mapping.md)
- 头文件：`include/mcp/mcp_server.h`
