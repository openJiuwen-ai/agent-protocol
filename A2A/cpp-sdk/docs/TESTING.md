# A2A C++ SDK 测试说明

## 目录结构

```
tests/
  CMakeLists.txt
  ut/
    CMakeLists.txt
    fixtures/          # 共享 mock 与辅助工具
    client/
    server/
    shared/
    transport/
    event/
    log/
    utils/
```

- 测试源文件命名 `test_<module>.cpp`，尽量与 `src/<module>/` 对应。
- 共享辅助代码位于 `tests/ut/fixtures/`（端口分配、mock executor/task store、AgentCard 构造等）。
- `a2a_ut_test` 二进制链接 `liba2a.so`，不重复编译全部 SDK 源码。

## 构建与运行

```bash
# 构建（Debug）+ 运行 ctest + 覆盖率报告（默认）
bash scripts/run_ut.sh

# 跳过覆盖率，更快
bash scripts/run_ut.sh --no-coverage

# 跳过 AddressSanitizer
bash scripts/run_ut.sh --no-coverage --no-asan
# 或
A2A_SKIP_ASAN=1 bash scripts/run_ut.sh --no-coverage
```

`run_ut.sh` 默认启用 **AddressSanitizer** 与 **覆盖率**（gcovr）。未安装 `gcovr` 时会跳过 HTML 报告并给出提示。

手动构建：

```bash
bash scripts/build.sh -u -t Debug
cd build && ctest -V --output-on-failure
```

直接运行测试二进制时需设置库路径：

```bash
export LD_LIBRARY_PATH=output/lib:build:${LD_LIBRARY_PATH}
./build/tests/ut/a2a_ut_test
```

## 编写约定

- 需要 bind/listen 的测试优先使用 `A2A::Test::GetFreeTcpPort()`（`fixtures/test_network.h`），避免硬编码 `8080` 等固定端口。
- 复用 `tests/ut/fixtures/` 中的 mock：
  - `mock_agent_executor.h` → `DummyAgentExecutor`
  - `mock_task_store.h` → `DummyTaskStore`
  - `test_agent_card.h` → `MakeAgentCard()`
- 禁止占位测试（如 `EXPECT_TRUE(true)`）；每个用例须断言真实行为。
- 网络集成测试使用 `127.0.0.1` 与临时端口，除非用例明确需要固定 URL 字符串。

## 覆盖率

```bash
bash scripts/run_ut.sh
# HTML 报告：build/coverage_report.html（需安装 gcovr：pip install gcovr）
```

## 相关文档

- [README.md](../README.md) — 快速入门与构建选项
- [API.md](API.md) — 公开 API 参考
- [LOGGING.md](LOGGING.md) — 日志说明；测试参考 `tests/ut/log/`
