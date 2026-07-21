# A2A C++ SDK 测试说明

## 概述

测试基于 **Google Test**，通过 **CTest** 发现与运行。测试源码位于 `tests/ut/`，默认**不编译**（需显式开启 `A2A_ENABLE_TESTS`）。

构建与跑测脚本（`build.sh`、`run_ut.sh`、`install_deps.sh`）均为 **Bash**，请使用 `bash scripts/...` 调用，勿用 `sh`（详见 [依赖说明 - 脚本解释器](dependencies.md#脚本解释器说明)）。

## 快速运行

```bash
cd A2A/cpp-sdk

# 1. Debug + 启用测试 + 运行 ctest（默认含覆盖率与 ASAN）
bash scripts/run_ut.sh

# 2. 更快：跳过覆盖率与 ASAN
bash scripts/run_ut.sh --no-coverage --no-asan
```

手动构建后运行：

```bash
bash scripts/build.sh -u -t Debug
cd build
export LD_LIBRARY_PATH="../output/lib:${LD_LIBRARY_PATH:-}"
ctest --output-on-failure -V
```

## 构建选项

通过 `scripts/build.sh` 传入：

| 选项 | CMake 变量 | 说明 |
|------|------------|------|
| `-t, --type <type>` | `CMAKE_BUILD_TYPE` | 构建类型：`Debug`、`Release`、`RelWithDebInfo`、`MinSizeRel`（默认 `Release`） |
| `-u, --with-tests` | `A2A_ENABLE_TESTS=ON` | 编译单元测试 |
| `-c, --coverage` | `A2A_ENABLE_COVERAGE=ON` | 启用覆盖率（隐含 `--with-tests`） |
| `--no-client` | `A2A_BUILD_CLIENT=OFF` | 跳过 Client 模块 |
| `--no-server` | `A2A_BUILD_SERVER=OFF` | 跳过 Server 模块 |
| `--asan` | `ASAN=enable` | 启用 AddressSanitizer（Debug） |

`run_ut.sh` 默认启用 **AddressSanitizer** 与 **覆盖率**（gcovr）。可通过 `--no-coverage`、`--no-asan` 或 `A2A_SKIP_ASAN=1` 关闭。

## 覆盖率与报告

```bash
# 带覆盖率编译并运行（run_ut.sh 默认）
bash scripts/run_ut.sh

# 仅构建覆盖率
bash scripts/build.sh -t Debug -c
```

报告默认输出：

| 文件 | 说明 |
|------|------|
| `build/coverage_report.html` | 覆盖率 HTML（需 gcovr） |

`run_ut.sh` 会在缺少 gcovr 时跳过 HTML 报告并给出安装提示（`pip install gcovr`）。

## 测试模块结构

```
tests/ut/
├── client/          # 客户端、Card Resolver、Transport
├── server/          # 服务端、HTTP Server、Request Handler
├── shared/          # 错误、HTTP、UUID、定时器等
├── transport/       # HTTP Server Transport、流式发射器
├── event/           # 事件系统
├── log/             # 日志模块
├── utils/           # 消息工具、ID 生成
└── fixtures/        # 共享 mock 与辅助工具
```

编写约定：

- 测试源文件命名 `test_<module>.cpp`，尽量与 `src/<module>/` 对应。
- `a2a_ut_test` 二进制链接 `liba2a.so`，不重复编译全部 SDK 源码。
- 需要 bind/listen 的测试优先使用 `A2A::Test::GetFreeTcpPort()`（`fixtures/test_network.h`），避免硬编码端口。
- 复用 `fixtures/` 中的 mock（`mock_agent_executor.h`、`mock_task_store.h`、`test_agent_card.h` 等）。
- 禁止占位测试（如 `EXPECT_TRUE(true)`）；每个用例须断言真实行为。

## 常见问题

### 构建目录不存在

```
Error: Build directory not found
```

先执行：`bash scripts/build.sh -u -t Debug`

### 无测试用例

确认构建时使用了 `--with-tests`，且未使用 `--no-client` / `--no-server` 裁剪掉被测模块。

### 运行时找不到 liba2a.so

```bash
export LD_LIBRARY_PATH="$(pwd)/output/lib:$(pwd)/build:${LD_LIBRARY_PATH:-}"
```

### CMake 配置失败（stale FetchContent 缓存）

从其他机器 rsync 后可能出现路径不一致。清理后重编：

```bash
rm -rf third_party/*-subbuild third_party/*-build build _deps
bash scripts/run_ut.sh --no-coverage --no-asan
```

### 端口冲突

部分测试使用动态端口；若仍失败，可单独运行模块测试：

```bash
cd build
ctest -R LoggerTest --output-on-failure
```

## 与示例的关系

- **单元测试**：验证 SDK 内部模块与协议逻辑。
- **示例冒烟**：`scripts/run_example.sh` 验证可执行示例链路，不属于 CTest。

两者互补，发布前建议均跑通。

## 相关文档

- [README.md](../README.md) — 快速入门与构建选项
- [api/README.md](api/README.md) — 公开 API 索引
- [logging.md](logging.md) — 日志说明
