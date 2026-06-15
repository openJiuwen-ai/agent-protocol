# MCP Client Example 说明

## 目录结构

```
client_example/
├── tool_example/             # 初始化、Ping、Tool、Progress、Complete
├── prompt_example/           # Prompt
├── resource_example/         # Resource 订阅与读取
├── sampling_example/         # 服务端发起的 Sampling
└── run_all_examples.sh       # 依次运行 tool / prompt / resource（不含 sampling）
```

## 示例内容

| 示例 | 主要 API |
|------|----------|
| `tool_example` | `Initialize`、`SendPing`、`ListTools`、`CallTool`、`Complete` |
| `prompt_example` | `ListPrompts`、`GetPrompt` |
| `resource_example` | `ListResources`、`ReadResource`、`SubscribeResource` |
| `sampling_example` | `SetSamplingCreateMessageCallback`、`CallTool`（触发服务端 sampling） |

## 前置条件

1. 已编译 SDK：

```bash
cd MCP/cpp-sdk
./scripts/build.sh
```

产物路径：`../../output/lib/libmcp.so`（相对于各示例子目录）。头文件使用源码树 `../../include/mcp/`。

2. MCP Server 已启动，默认端点：`http://127.0.0.1:8000/mcp`

推荐一键启动（在 `MCP/cpp-sdk` 下）：

```bash
./scripts/run_example.sh -t server    # 终端 1：后台 Server
./scripts/run_example.sh -t tool      # 终端 2：Client 示例
```

或运行全部：

```bash
./scripts/run_example.sh -t all
```

## 运行单个示例

```bash
cd tool_example && ./run_example.sh
cd prompt_example && ./run_example.sh
cd resource_example && ./run_example.sh
cd sampling_example && ./run_example.sh
```

指定端口（需与 Server 一致）：

```bash
./run_example.sh --port=9000
```

## 运行多个示例（不含 sampling）

```bash
./run_all_examples.sh
```

## 相关文档

- [Server 示例](../server_example/README.md)
- [Client API](../../docs/api/client.md)
- [主 README](../../README.md)
