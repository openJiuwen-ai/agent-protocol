# MCP C++ SDK 测试说明

## 概述

测试基于 **Google Test**，通过 **CTest** 发现与运行。测试源码位于 `tests/ut/`，默认**不编译**（需显式开启 `MCP_ENABLE_TESTS`）。

## 快速运行

```bash
cd MCP/cpp-sdk

# 1. Debug + 启用测试
./scripts/build.sh -t Debug --with-tests

# 2. 运行全部测试
cd build
ctest --output-on-failure -V
```

## 构建选项

通过 `scripts/build.sh` 传入：

| 选项 | CMake 变量 | 说明 |
|------|------------|------|
| `-u, --with-tests` | `MCP_ENABLE_TESTS=ON` | 编译单元测试 |
| `-c, --coverage` | `MCP_ENABLE_COVERAGE=ON` | 启用覆盖率（隐含 `--with-tests`） |
| `-s, --stdio` | `MCP_ENABLE_STDIO=ON` | 启用 stdio 相关 UT |
| `--no-client` | `MCP_BUILD_CLIENT=OFF` | 跳过 Client 模块测试 |
| `--no-server` | `MCP_BUILD_SERVER=OFF` | 跳过 Server 模块测试 |
| `--no-http` | `MCP_WITH_HTTP=OFF` | 跳过 HTTP 相关测试 |

当 Client + Server + HTTP 均启用时，会自动打开 **ST 集成测试**（`tests/ut/st/`，`MCP_ENABLE_ST_TESTS`）。

## 覆盖率与报告

```bash
# 1. 带覆盖率编译
./scripts/build.sh -t Debug --coverage

# 2. 运行测试并生成 HTML 报告
./scripts/run_ut.sh
```

报告默认输出目录：`test_output/`

| 文件 | 说明 |
|------|------|
| `test_output/ut/ut_result.html` | 测试结果 |
| `test_output/coverage/` | 覆盖率 HTML（需 gcovr） |

`run_ut.sh` 会在缺少 gcovr 时尝试通过 pip 安装。

## 测试模块结构

```
tests/ut/
├── client/          # 客户端、HTTP 传输、TLS
├── server/          # 服务端、Manager、HTTP Server
├── shared/          # jsonrpc、base_session
├── auth/            # 鉴权
├── transport/       # stdio 传输
├── net/             # 网络层
├── event/           # 事件系统
├── log/             # 日志模块
└── st/              # 端到端集成（可选）
```

## 常见问题

### 构建目录不存在

```
Error: Build directory not found
```

先执行：`./scripts/build.sh -t Debug --with-tests`

### 无测试用例

确认构建时使用了 `--with-tests`，且未使用 `--no-client` 等裁剪掉被测模块。

### 集成测试端口冲突

部分测试使用固定端口（如 8001）。并行运行或端口占用时可能失败，可单独运行模块测试：

```bash
cd build
ctest -R mcp_client_tests --output-on-failure
```

## 与示例的关系

- **单元测试**：验证 SDK 内部模块与协议逻辑。
- **示例冒烟**：`scripts/run_example.sh` 验证可执行示例链路，不属于 CTest。

两者互补，发布前建议均跑通。
