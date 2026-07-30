# 测试说明

本目录使用 `pytest` 验证 A4P SDK 的核心数据结构、授权安全边界、持久化能力、
HTTP 协议适配，以及示例程序中的浏览器授权流程。

## 用例分层与覆盖范围

| 目录 | 覆盖范围 |
| --- | --- |
| `unit/` | 纯 JSON wire types、服务端签名密钥配置、客户端默认值与异常映射、凭据与用量存储、意图 scope 匹配、mandate 与 intent token 的签名/篡改/身份绑定校验、Ed25519 与 WebAuthn 注册和签名流程 |
| `integration/` | operation/intent 授权的 prepare-complete 全流程、并发与重放保护、待处理授权状态校验、无签名模式、执行策略及配额持久化、HTTP 路由和真实 HTTP 客户端/服务端交互 |
| `examples/` | `examples/note_mcp_a4p` 浏览器用户授权器的授权确认、可信签名选项覆盖、静态资源加载和 WebAuthn 凭据注册回传 |

各测试文件的主要关注点如下：

- `unit/test_types_and_keys.py`：wire types 的 JSON 序列化，以及服务端签名密钥的开发/生产环境约束。
- `unit/test_client.py`：客户端配置归一化、HTTP 错误和传输错误映射。
- `unit/test_stores.py`：JSON 凭据存储的持久化、跨进程刷新和旧格式拒绝，以及 SQLite 用量存储的原子消费和过期清理。
- `unit/test_intent_scope.py`：意图参数的精确/通配符匹配、同名 action 候选、额外参数策略及其签名保护。
- `unit/test_mandate_security.py`：随机授权 ID、challenge 绑定、服务端签名验证、过期和篡改防护。
- `unit/test_intent_token_security.py`：token 签名元数据、scope、有效期、身份绑定、规范 JSON 和稳定的失败关闭错误码。
- `unit/test_user_signatures_and_webauthn.py`：签名方法配置、Ed25519/WebAuthn 凭据注册、签名封装、验证失败分支和计数器更新。
- `integration/test_operation_authorization.py`：operation 授权完成、跨请求 mandate 拒绝、重放防护、无签名和 WebAuthn 路径。
- `integration/test_operation_authorization_flow.py`：授权与当前 operation 的一致性、并发单次执行、业务失败后的授权消费、过期/丢失状态。
- `integration/test_intent_authorization.py`：intent 授权、agent key 绑定、执行策略、用量持久化、无签名模式和自定义展示文本。
- `integration/test_http.py`：HTTP 路由、端口配置、协议状态码，以及真实 socket 上的公开客户端 API。
- `examples/test_browser_user_authorizer.py`：浏览器授权器的签名、可信选项替换、页面资源和注册凭据回传。

## 执行测试

在仓库根目录执行。项目要求 Python 3.10 或更高版本，并建议使用
[uv](https://docs.astral.sh/uv/) 管理环境。

执行全部测试：

```bash
uv run --with pytest pytest
```

按分层执行：

```bash
uv run --with pytest pytest tests/unit
uv run --with pytest pytest tests/integration
uv run --with pytest pytest tests/examples
```

执行单个测试文件或单个用例：

```bash
uv run --with pytest pytest tests/unit/test_intent_scope.py
uv run --with pytest pytest \
  tests/unit/test_intent_scope.py::test_params_match_intent_token_glob_and_exact
```

常用调试参数：

```bash
# 输出更详细的用例名称
uv run --with pytest pytest -vv

# 首个失败后停止，并保留标准输出
uv run --with pytest pytest -x -s

# 只收集并展示用例，不执行
uv run --with pytest pytest --collect-only -q
```

HTTP 集成测试仅监听本机回环地址，并使用操作系统分配的临时端口，不依赖外部
A4P 服务。WebAuthn 用例通过 mock 验证协议输入输出，不要求真实浏览器或硬件
认证器。
