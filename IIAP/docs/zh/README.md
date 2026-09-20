# IIAP SDK 中文文档

最后更新：2026-09-20

IIAP（Implicit Intent Aware Protocol，隐式意图感知协议）观察 A2UI 的隐私安全语义事件，在端侧形成中性行为模式，
将 IntentContextPacket 交给后端判断是否提供可选、非阻塞帮助，并把用户对建议的反馈
关联回同一 packet 和 decision。

当前源码候选为 `0.1.0-rc.1`，内置支持 A2UI v0.8。npm/PyPI 尚未公开发布。

## 文档入口

| 目标 | 文档 |
|---|---|
| 构建 SDK 并完成首次接入 | [快速开始](quickstart.md) |
| 查询 TypeScript/Python 稳定接口 | [API 参考](api-reference.md) |
| 理解 wire、隐私和安全更新边界 | [协议与安全](protocol-and-security.md) |
| 执行测试、查看兼容范围和发布门 | [测试与兼容性](testing-and-compatibility.md) |

## 当前模块

- `@openjiuwen/iiap`：Runtime、公共 DTO、隐私和 suggestion 安全校验；
- `@openjiuwen/iiap/a2ui-v08`：A2UI v0.8 Adapter；
- `@openjiuwen/iiap/decision`：模型 decision 与服务端 envelope 解析；
- `@openjiuwen/iiap/http`：可选 HTTP Transport；
- `@openjiuwen/iiap/react`：可选 React Presenter；
- `openjiuwen-iiap`：Python prompt、validator 和参考 Service。

Runtime 支持 Host 托管和 SDK 托管两种模式，但同一实例只能选择一种 packet 发送方式。
SDK 不自动提交表单、调用 action、支付或下单；更新必须由用户明确接受，并在执行时按原
packet 的 `surface/path/value` 白名单再次校验。

JSON Schema、源码和通过的测试共同定义当前行为。A2UI v0.9.1 只保留 wire version，
当前没有默认 Adapter；接入方必须实现并验证自定义 Adapter。
