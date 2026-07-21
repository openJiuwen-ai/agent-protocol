# A2A C++ SDK API 文档

本目录为 **公共 API 使用说明**（手写文档）。完整声明见 `include/*.h`。

## 文档索引

| 文档 | 内容 |
|------|------|
| [client.md](client.md) | 客户端创建、Card 解析、消息发送、任务与流式事件 |
| [server.md](server.md) | 服务端创建、AgentExecutor、HTTP 监听 |
| [protocol-mapping.md](protocol-mapping.md) | A2A 协议方法 ↔ C++ API 对照 |
| [../errors.md](../errors.md) | 错误码、异常类型与处理建议 |
| [../logging.md](../logging.md) | SDK 日志与协议事件说明 |

## 头文件一览

| 头文件 | 说明 |
|--------|------|
| `types.h` | 协议 DTO、`A2AErrorCode`、`Message`、`Task`、`AgentCard` 等 |
| `error.h` | `A2AClientException`、`A2AServerError` |
| `a2a_log.h` | `A2A_LOG`、`SetLogLevel`、`SetLogCallback` |
| `client/client.h` | `Client` 高层 API |
| `client/client_factory.h` | `ClientFactory` |
| `client/a2a_card_resolver.h` | `A2ACardResolver` |
| `client/http_card_resolver_builder.h` | HTTP Card 解析器工厂 |
| `client/jsonrpc_transport.h` | JSON-RPC 传输抽象 |
| `client/client_call_interceptor.h` | 请求拦截器 |
| `server/server.h` | `Server` 生命周期 |
| `server/http_server_builder.h` | `HttpServerBuilder`、`HttpConfig` |
| `server/agent_executor.h` | `AgentExecutor` |
| `server/task_store.h` | `TaskStore` |
| `server/task_updater.h` | `TaskUpdater` |

## 架构概览

```
HttpCardResolverBuilder / ClientFactory
        ↓
   Client + ClientTransport（JSON-RPC）
        ↓
   HTTP → Agent Server（/jsonrpc）

HttpServerBuilder
        ↓
   Server + AgentExecutor + TaskStore
        ↓
   HTTP JSON-RPC + /.well-known/agent-card.json
```

## 示例与测试

| 能力 | 示例 | 测试（参考） |
|------|------|--------------|
| Client 非流式 | `examples/helloworld_client.cpp` | `tests/ut/client/` |
| Server 非流式 | `examples/helloworld_server.cpp` | `tests/ut/server/` |
| Client 流式 | `examples/streaming_client.cpp` | `tests/ut/transport/` |
| Server 流式 | `examples/streaming_server.cpp` | `tests/ut/server/` |
| Card Resolver | `examples/helloworld_client.cpp` | `tests/ut/client/test_http_card_resolver.cpp` |

## 相关文档

- [依赖说明](../dependencies.md)
- [测试说明](../testing.md)
- [错误处理说明](../errors.md)
- [日志说明](../logging.md)
- [主 README](../../README.md)
