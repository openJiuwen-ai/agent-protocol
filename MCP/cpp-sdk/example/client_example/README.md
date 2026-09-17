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

**双终端（推荐调试 Client 时）**

```bash
# 终端 1（MCP/cpp-sdk 目录）
sh scripts/run_example.sh -t server    # 前台常驻，Ctrl+C 停止

# 终端 2
sh scripts/run_example.sh -t tool
```

**单终端一键**

```bash
sh scripts/run_example.sh -t all
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

`tool_example` 鉴权模式（Server 需以 `--auth` 启动，默认连 `http://127.0.0.1:8001/mcp`）：

```bash
cd tool_example && ./run_example.sh --auth
```

## 运行多个示例（不含 sampling）

```bash
./run_all_examples.sh
```

## 相关文档

- [Server 示例](../server_example/README.md)
- [Client API](../../docs/api/client.md)
- [主 README](../../README.md)
