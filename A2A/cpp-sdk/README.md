# A2A C++ SDK

## 目录

- [概述](#概述)
- [文档](#文档)
- [环境要求](#环境要求)
- [快速入门](#快速入门)
- [构建与安装](#构建与安装)
- [运行示例](#运行示例)
- [运行测试](#运行测试)
- [参与贡献](#参与贡献)
- [License](#license)

## 概述

A2A C++ SDK 是 [Agent-to-Agent 协议 v1.0](https://a2a-protocol.org/v1.0.0/specification/) 的 C++ 实现，提供 A2A 客户端与服务器能力，支持基于 **HTTP + JSON-RPC** 的智能体间通信。

- 构建 A2A 客户端：发现 Agent Card、发送消息、查询/取消任务、接收流式事件
- 构建 A2A 服务器：注册 `AgentExecutor`、管理任务生命周期、暴露 JSON-RPC 端点
- 支持多模态 `Part`、推送通知配置、请求拦截器、自定义 `TaskStore` 等扩展能力（详见示例与 API 文档）

## 文档

| 文档 | 说明 |
|------|------|
| [依赖说明](docs/dependencies.md) | 运行/编译依赖、版本、各发行版安装命令 |
| [测试说明](docs/testing.md) | 单元测试、覆盖率、ASAN |
| [API 文档索引](docs/api/README.md) | Client / Server API、协议对照 |
| [错误处理说明](docs/errors.md) | 错误码、异常类型与处理建议 |
| [日志说明](docs/logging.md) | SDK 诊断日志、`A2A_LOG` / `SetLogCallback` 用法 |

## 环境要求

- **操作系统**：Linux（glibc 或 musl，主要验证平台）
- **编译器**：C++17 及以上（GCC 9+ / Clang 10+）
- **CMake**：≥ 3.15（见根目录 `CMakeLists.txt`）
- **依赖**：见 [docs/dependencies.md](docs/dependencies.md)

## 快速入门

在仓库 `A2A/cpp-sdk` 目录下，按以下顺序执行（约 5–15 分钟，视网络与编译环境而定）：

```bash
# 1. 安装系统依赖（curl、openssl、libevent 等；部分发行版需 sudo）
sudo bash scripts/install_deps.sh

# 2. 编译 SDK 与示例（产物：output/lib/liba2a.so、output/bin/*）
bash scripts/build.sh -e

# 3. 一键冒烟测试全部示例（脚本内会先后台起 Server，再跑 Client）
bash scripts/run_example.sh
```

默认示例端口：`8888`。JSON-RPC 端点：`http://127.0.0.1:8888/jsonrpc`。指定端口示例：

```bash
A2A_EXAMPLE_PORT=9000 bash scripts/run_example.sh
```

**方式一：单终端一键（推荐首次验证）**

```bash
bash scripts/run_example.sh   # 依次跑 helloworld / streaming：后台起 Server → Client 检查 → 停止 Server
```

**方式二：双终端（Server 常驻，便于反复调试 Client）**

```bash
export LD_LIBRARY_PATH="$(pwd)/output/lib:${LD_LIBRARY_PATH:-}"

# 终端 1：前台运行 Server（Ctrl+C 停止）
./output/bin/helloworld_server -i 127.0.0.1 -p 8888

# 终端 2：Server 已监听后再跑 Client
./output/bin/helloworld_client -i 127.0.0.1 -p 8888
```

`streaming_server` / `streaming_client` 同理，须先 Server 后 Client，且两端端口一致。

**注意**：

- **必须先有 Server 再跑 Client**；同一终端里直接连跑两条命令时，Client 会因 Server 未就绪而失败。
- `run_example.sh`：无参数时在同一脚本内后台起 Server、跑 Client 后自动清理进程。
- 手动启动前须先 `bash scripts/build.sh -e`，并设置 `LD_LIBRARY_PATH`（见下文「运行示例」）。

## 构建与安装

统一入口为 `scripts/build.sh`（不是仓库根目录下的 `build.sh`）。`scripts/` 下脚本均为 **Bash**，请使用 `bash scripts/...` 调用，勿用 `sh`（见 [依赖说明 - 脚本解释器](docs/dependencies.md#脚本解释器说明)）。

```bash
# Release 构建（默认，仅核心库）
bash scripts/build.sh

# Release 构建 + 示例
bash scripts/build.sh -e

# Debug 构建（含调试符号）
bash scripts/build.sh -t Debug

# 编译并启用单元测试
bash scripts/build.sh -u -t Debug

# 覆盖率构建 + 运行测试报告（见 docs/testing.md）
bash scripts/build.sh -c

# AddressSanitizer 调试构建
bash scripts/build.sh -u -t Debug --asan
```

常用 CMake 裁剪选项（经 `build.sh` 传入）：

| 选项 | 含义 |
|------|------|
| `-h, --help` | 显示帮助信息并退出 |
| `-e, --with-examples` | 编译 `examples/` 下示例程序 |
| `-u, --with-tests` | 编译单元测试 |
| `-c, --coverage` | 启用覆盖率（隐含 `-u`，Debug） |
| `--no-client` | 不编译 Client 组件 |
| `--no-server` | 不编译 Server 组件 |
| `--asan` | 启用 AddressSanitizer（自动使用 Debug） |
| `-t, --type <type>` | CMake 构建类型：`Debug`、`Release`、`RelWithDebInfo`、`MinSizeRel`（默认 `Release`；日常开发/测试常用 `Debug`，发布用 `Release`） |
| `-b, --build-dir <dir>` | 构建目录，默认 `build` |
| `-g, --generator <name>` | CMake 生成器（如 `Ninja`、`NMake Makefiles`） |

**构建产物**：

| 路径 | 说明 |
|------|------|
| `output/lib/liba2a.so` | 共享库 |
| `output/include/` | 公共头文件（`types.h`、`client/`、`server/` 等） |
| `output/bin/` | 示例二进制（需 `-e`） |

集成到自己工程时，链接 `liba2a.so` 并添加头文件路径 `output/include`（或源码树 `include/`）。运行时需要将 `output/lib` 加入 `LD_LIBRARY_PATH`。详见 `examples/CMakeLists.txt`。

## 运行示例

示例位于 `examples/` 目录。可使用统一脚本 `scripts/run_example.sh` 做冒烟测试，也可手动分终端启动 Server 与 Client。

**方式一：单终端（`run_example.sh`）**

```bash
bash scripts/build.sh -e
bash scripts/run_example.sh
```

脚本会对 `helloworld`、`streaming` 各执行一轮：**后台起 Server → HTTP/JSON-RPC 检查 → 跑 Client → 停止 Server**。

**方式二：双终端（手动，Server 常驻）**

```bash
export LD_LIBRARY_PATH="$(pwd)/output/lib:${LD_LIBRARY_PATH:-}"

# 终端 1
./output/bin/helloworld_server -i 127.0.0.1 -p 8080

# 终端 2（须等 Server 监听后再执行）
./output/bin/helloworld_client -i 127.0.0.1 -p 8080
```

**注意**：

- **必须先有 Server 再跑 Client**；Server 在前台运行时会占用终端，Client 应在另一终端连接同一 `-i` / `-p`。
- 运行示例二进制前须 `bash scripts/build.sh -e`，并将 `output/lib` 加入 `LD_LIBRARY_PATH`。

| 示例 | 源文件 | 说明 |
|------|--------|------|
| Hello World Server | `examples/helloworld_server.cpp` | HTTP Server，非流式消息处理 |
| Hello World Client | `examples/helloworld_client.cpp` | 获取 Agent Card、发送消息 |
| Streaming Server | `examples/streaming_server.cpp` | 流式任务状态与产物推送 |
| Streaming Client | `examples/streaming_client.cpp` | `message/stream` 流式接收 |

API 与协议细节见 [docs/api/README.md](docs/api/README.md)。

## 运行测试

```bash
# 构建 + 运行 ctest（含覆盖率与 ASAN，默认）
bash scripts/run_ut.sh

# 跳过覆盖率，更快
bash scripts/run_ut.sh --no-coverage

# 跳过 ASAN
bash scripts/run_ut.sh --no-coverage --no-asan
# 或
A2A_SKIP_ASAN=1 bash scripts/run_ut.sh --no-coverage
```

也可手动构建后运行：

```bash
bash scripts/build.sh -u -t Debug
cd build
export LD_LIBRARY_PATH="../output/lib:${LD_LIBRARY_PATH:-}"
ctest --output-on-failure
```

覆盖率与报告说明见 [docs/testing.md](docs/testing.md)。

## 日志

SDK 默认将诊断日志输出到 **stdout**，输出前缀含 `[LEVEL]`（`DEBUG` / `INFO` / `WARN` / `ERROR` / `FATAL`）。若 stdout 被应用占用，可通过 `SetLogCallback` 重定向到 stderr 或文件。详见 [docs/logging.md](docs/logging.md)。

## 参与贡献

欢迎提交 Issue、文档改进与代码贡献。提交 PR 前请运行：

```bash
bash scripts/run_ut.sh --no-coverage
```

确保单元测试通过。

## License

本项目依据 Apache-2.0 许可证授权。

## Copyright

Copyright (c) 2025-2026 Huawei Technologies Co., Ltd. All rights reserved.

## Third-Party Notices

本项目包含或依赖第三方开源软件，其版权和许可证信息均归原作者所有，并在相应文件中予以说明。
