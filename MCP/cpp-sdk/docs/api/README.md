# MCP C++ SDK API 文档

本目录为 **公共 API 使用说明**（手写文档）。完整声明见 `include/mcp/*.h`。

## 文档索引

| 文档 | 内容 |
|------|------|
| [client.md](client.md) | 客户端创建、初始化、RPC 调用、回调 |
| [server.md](server.md) | 服务端创建、注册 Tool/Prompt/Resource、运行 |
| [protocol-mapping.md](protocol-mapping.md) | MCP 协议方法 ↔ C++ API 对照 |
| [../errors.md](../errors.md) | 错误码、异常类型与处理建议 |
| [../logging.md](../logging.md) | SDK 日志与协议日志说明 |

## 头文件一览

| 头文件 | 说明 |
|--------|------|
| `mcp_client.h` | `McpClient`、`McpClientFactory` |
| `mcp_server.h` | `McpServer`、`McpServerFactory`、`ToolFunc` 等 |
| `mcp_type.h` | 配置、DTO、回调类型 |
| `mcp_error.h` | `MCPError`、`JsonRpcErrorCode` |
| `mcp_auth.h` | `AuthProvider`、`Authenticator`、`Authorizer` |
| `mcp_log.h` | `MCP_LOG`、`SetLogLevel`、`SetLogCallback` |

## 架构概览

```
McpClientFactory / McpServerFactory
        ↓
   McpClient / McpServer（抽象接口）
        ↓
 ClientSession / McpServerImplement + Transport
```

## 示例与测试

| 能力 | 示例 | 测试（参考） |
|------|------|--------------|
| Client HTTP | `example/client_example/tool_example/` | `tests/ut/client/client_test.cpp` |
| Server HTTP | `example/server_example/` | `tests/ut/server/` |
| Sampling | `example/client_example/sampling_example/` | `tests/ut/client/test_client_session_sampling_create_message.cpp` |
| Auth | `example/server_example/`（`--auth`） | `tests/ut/auth/` |

## 相关文档

- [依赖说明](../dependencies.md)
- [测试说明](../testing.md)
- [错误处理说明](../errors.md)
- [日志说明](../logging.md)
- [主 README](../../README.md)
