# IIAP 示例

[English](README.md) | 简体中文

这些示例说明 IIAP（Implicit Intent Aware Protocol，隐式意图感知协议）在 A2UI 应用中的接入位置。它们是本地集成参考，不是生产服务，也不是协议一致性测试。

## 每个示例解决什么问题

| 示例 | 用途 | 是否依赖外部模型或服务 |
|---|---|---|
| `typescript/complete-v08.ts` | 完整客户端链路：A2UI v0.8 Adapter、Runtime 观察、packet 交付、decision 展示、接受后的 assistance，以及安全更新执行 | 否，使用确定性的内存 Transport |
| `typescript/minimal.ts` | 最小 Adapter/Runtime 生命周期：生成 plan、激活、释放资源 | 否 |
| `browser/` | 在浏览器中查看 React HelpPresenter，以及接受/关闭反馈 | 否 |
| `python/complete_service.py` | 后端 `DecisionService` 和 `AssistanceService` 链路，包括 canonical packet 校验和响应 envelope | 否，使用确定性的 ModelAdapter |
| `python/minimal.py` | 展示 Host 如何把自己的 ModelAdapter 和 packet 交给 `DecisionService` | 由 Host 提供 |

完整 TypeScript/Python 示例有意使用确定性的内存实现，因此不需要凭据或外部服务，同时仍会执行真实 SDK 的校验和编排逻辑。

## 环境准备

在 `IIAP/` 目录执行：

```bash
npm ci
npm run build
uv sync --project implementations/python --extra test
```

## 运行完整 TypeScript 链路

```bash
node implementations/typescript/dist/examples/typescript/complete-v08.js
```

预期结果：终端先输出 `offer:`，再输出 `assistance:`。Presenter 会自动接受建议，使示例无需 UI 即可结束。接入真实应用时，应将 `DemoTransport` 和 `AutoAcceptPresenter` 替换为 Host 实现。

## 运行完整 Python 服务链路

```bash
PYTHONPATH=implementations/python/src \
  uv run --project implementations/python python examples/python/complete_service.py
```

预期结果：终端输出相互关联的 `iiap.decision` envelope 和 `iiap.assistance.response` envelope。示例读取 canonical packet fixture，不会发送网络请求。

## 构建浏览器 Presenter Demo

```bash
npm run demo:build
```

构建产物位于 `build/browser-demo/`。该 Demo 用于查看默认 React Presenter 和反馈交互，不包含真实 A2UI Renderer 或模型后端。

## 最小代码片段

`typescript/minimal.ts` 和 `python/minimal.py` 是刻意保持精简的 Host 侧片段，只说明生命周期和服务调用边界，不是可独立验收的端到端 Demo。

生产接入请继续阅读[快速开始](../docs/zh/quickstart.md)和[协议与安全](../docs/zh/protocol-and-security.md)。
