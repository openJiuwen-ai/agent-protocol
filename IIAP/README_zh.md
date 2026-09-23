# IIAP SDK

简体中文 | [English](README.md)

## 摘要

IIAP（Implicit Intent Aware Protocol，隐式意图感知协议）用于从 A2UI 的本地组件交互中提取隐私安全的语义事件，在端侧聚合为意图上下文，由后端判断是否提供可选、非阻塞的帮助，并将用户反馈关联回原始上下文。

本目录是可独立构建的 IIAP SDK，可原样迁移至 `agent-protocol/IIAP`。当前版本为 `0.1.0-rc.1`，内置支持 A2UI v0.8；npm/PyPI 制品尚未发布到公共 registry。

## 主要特性

- **端侧感知**：把 A2UI v0.8 组件定义转换为观察计划，采集选择变化、校验失败、停留等语义元数据；
- **隐私保护**：不上传原始输入值、稳定值哈希或完整 data model，只传递短期 token 和中性行为模式；
- **非阻塞帮助**：支持文字帮助和经用户确认的数据更新建议，不自动提交表单或调用业务 action；
- **安全更新**：接受建议时依据原 packet 的 `surface/path/value` 白名单再次校验，并要求整批原子执行；
- **灵活接入**：Runtime 支持 Host 托管现有信道，或通过 SDK Transport 发起请求；同一实例只能选择一种模式；
- **跨语言服务**：提供 TypeScript Runtime/Adapter/Presenter 和 Python Decision/Assistance Service。

## 快速开始

环境要求：Node.js 20+、Python 3.11+。在 `IIAP/` 目录执行：

```bash
npm ci
npm run build
uv sync --project implementations/python --extra test
```

运行一条不依赖外部模型或服务的完整示例：

```bash
node implementations/typescript/dist/examples/typescript/complete-v08.js
```

示例会构造 A2UI v0.8 surface，产生本地交互事件，生成 packet，通过内存 Transport 返回帮助建议，并模拟用户接受和 assistance 响应。详细接入步骤见[快速开始](docs/zh/quickstart.md)。

## 模块

| 模块 | 用途 |
|---|---|
| `@openjiuwen/iiap` | Runtime、公共 DTO、隐私和 suggestion 安全校验 |
| `@openjiuwen/iiap/a2ui-v08` | A2UI v0.8 Adapter |
| `@openjiuwen/iiap/decision` | 模型 decision 与服务端 envelope 解析 |
| `@openjiuwen/iiap/http` | 可选 HTTP Transport |
| `@openjiuwen/iiap/react` | 可选 React Presenter |
| `openjiuwen-iiap` | Python prompt、validator 和参考 Service |

## 示例

- `examples/typescript/complete-v08.ts`：完整的端侧观察、决策、展示、帮助和安全更新链路；
- `examples/browser/`：React HelpPresenter 的浏览器展示和接受/关闭反馈；
- `examples/python/complete_service.py`：DecisionService 与 AssistanceService 的后端调用链。

完整说明和运行命令见 [examples/README_zh.md](examples/README_zh.md)。

## 测试

```bash
npm test
uv run --project implementations/python pytest implementations/python/tests -q
uv run --with jsonschema python scripts/check_contracts.py
uv run python scripts/check_docs.py
npm run packages:smoke
```

## 更多文档

- [中文文档入口](docs/zh/README.md)
- [快速开始](docs/zh/quickstart.md)
- [API 参考](docs/zh/api-reference.md)
- [协议与安全](docs/zh/protocol-and-security.md)
- [测试与兼容性](docs/zh/testing-and-compatibility.md)
- [第三方依赖技术清单](THIRD_PARTY_DEPENDENCIES.md)

## License

本项目依据 Apache-2.0 许可证授权。
