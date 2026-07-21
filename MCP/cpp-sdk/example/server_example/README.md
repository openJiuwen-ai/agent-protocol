# MCP Server Example 说明

## 功能

`server_example.cpp` 演示 Streamable HTTP Server，包含：

- Tool：`echo`、`async_echo`、`progress_tool`、sampling 相关工具等
- Prompt、Resource、Resource Template
- Completion
- 可选鉴权（`--auth`，端口 8001）
- 日志写入文件（`server_example.log`）

## 前置条件

```bash
cd MCP/cpp-sdk
./scripts/build.sh
```

共享库：`output/lib/libmcp.so`。头文件：`include/mcp/`。

## 运行

```bash
cd example/server_example
./run_example.sh
```

### 命令行参数

| 参数 | 说明 |
|------|------|
| `--port=<1-65535>` | 监听端口，默认 8000；endpoint 为 `http://127.0.0.1:<port>/mcp` |
| `--stateless` | 无状态模式（限制部分会话相关 API） |
| `--isJsonResponseDisable` | 禁用 JSON 响应模式 |
| `--auth` | 启用 Bearer 鉴权；未指定 `--port` 时 endpoint 为 `http://127.0.0.1:8001/mcp` |
| `-h`, `--help` | 帮助 |

`run_example.sh` 会将上述参数原样转发给 `ServerExample`。

示例：

```bash
./run_example.sh --port=9000
./run_example.sh --stateless --port=9000
./run_example.sh --auth
./run_example.sh --auth --port=8001
```

也可使用 `run_auth_example.sh`（等价于 `./run_example.sh --auth`，仅保留鉴权场景的提示输出）。

或使用统一脚本（`MCP/cpp-sdk` 目录）：

```bash
# 终端 1：前台 Server（保持运行）
sh scripts/run_example.sh -t server
sh scripts/run_example.sh -p 9000 -t server

# 终端 2：Client
sh scripts/run_example.sh -t tool
```

## 与 Client 联调

1. 在终端 1 运行 Server（`-t server` 为前台模式，不要用 `nohup sh scripts/run_example.sh -t server &`）
2. 确认端口在监听：`lsof -iTCP:8000 -sTCP:LISTEN`（或你的 `--port`）
3. 在终端 2 运行 Client 示例：

```bash
./scripts/run_example.sh -t tool
# 或
cd example/client_example/tool_example && ./run_example.sh
```

鉴权模式联调（端口 8001，token：`valid-token-12345`）：

```bash
# 终端 1
cd example/server_example && ./run_example.sh --auth

# 终端 2
cd example/client_example/tool_example && ./run_example.sh --auth
```

也可使用 `run_auth_example.sh`（与 `./run_example.sh --auth` 等价）。

## 日志

- 控制台：部分启动信息（`std::cout`）
- SDK 日志：`SetLogCallback(FileLogCallback)` 写入 `server_example/build/server_example.log`；级别由 `SetLogLevel` 统一过滤（见 [日志说明](../../docs/logging.md)）

## 相关文档

- [Client 示例](../client_example/README.md)
- [Server API](../../docs/api/server.md)
- [主 README](../../README.md)
