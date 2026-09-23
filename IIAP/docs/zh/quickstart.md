# 快速开始

最后更新：2026-09-23

本页用一条本地、确定性的示例链路验证 IIAP（Implicit Intent Aware Protocol，隐式意图感知协议）的核心流程：A2UI surface → 本地语义事件 → IntentContextPacket → decision → 用户反馈。示例不连接外部模型或服务。

## 1. 环境准备

要求：Node.js 20+、Python 3.11+。在 `IIAP/` 目录执行：

```bash
npm ci
npm run build
uv sync --project implementations/python --extra test
```

当前版本尚未发布到公共 registry。生产接入应固定源码提交，并使用 `npm run packages:smoke` 验证过的 npm tarball 和 Python wheel。

## 2. 先运行完整示例

```bash
node implementations/typescript/dist/examples/typescript/complete-v08.js
```

该示例会：

1. 用 A2UI v0.8 Adapter 为单选组件生成 `ObservationPlan`；
2. 模拟一次隐私安全的本地选择事件并生成 packet；
3. 通过内存 Transport 返回 `text_assistance` decision；
4. 用 Presenter 模拟用户接受，并请求 assistance。

终端应依次出现 `offer:` 和 `assistance:`。这证明 SDK 核心链路可运行，但不代表已经接入真实 Renderer、网络 Transport 或模型。

## 3. 把 A2UI 转成 ObservationPlan

```ts
import { A2UIV08Adapter } from '@openjiuwen/iiap/a2ui-v08';

const adapter = new A2UIV08Adapter({
  resolveAllowedValues: ({ declaredValues }) => declaredValues,
});

const plan = adapter.buildObservationPlans({
  sessionId: 'session-1',
  messageId: 'message-1',
  namespace: 'message-1',
  messages: finalA2UIV08Messages,
})[0];
```

只传入已经完成校验、修复并交给 Renderer 的 final message，不能观察流式草稿。没有 Host `SuggestionPolicy` 时，Adapter 不授权任何 UI 更新。

## 4. 选择一种 packet 交付模式

### Host 托管

适用于已有 WebSocket、RPC 或消息总线的系统：

```ts
import { createIIAPRuntime } from '@openjiuwen/iiap';

const runtime = createIIAPRuntime({
  onPacket: async (packet) => sendThroughExistingChannel(packet),
  onFeedback: async (feedback) => sendFeedbackThroughExistingChannel(feedback),
  feedbackUpload: true,
  presenter: hostPresenter,
  onAccept: applyAcceptedHelp,
});
```

Host 收到服务端 envelope 后调用 `session.handleDecision(envelope)`；外部请求取消时调用 `session.cancelDecision(packetId)`。

### SDK 托管

适用于标准请求/响应 HTTP 接口：

```ts
import { createIIAPRuntime } from '@openjiuwen/iiap';
import { HTTPDecisionTransport } from '@openjiuwen/iiap/http';

const runtime = createIIAPRuntime({
  transport: new HTTPDecisionTransport({ baseUrl: '/api/iiap' }),
  presenter,
  feedbackUpload: true,
  onAccept: applyAcceptedHelp,
});
```

`transport` 与 `onPacket` 互斥；同时配置会立即抛出配置错误。`feedbackUpload` 默认关闭；
启用后，Host 托管模式必须提供 `onFeedback`，SDK 托管模式的 Transport 必须实现
`sendFeedback`，否则创建 Runtime 时立即报错。

## 5. 激活并观察

```ts
if (!plan) throw new Error('No observable surface');
const session = runtime.createSession({ sessionId: plan.sessionId });
const handle = session.activate(plan, { focused: true });

handle.observe({
  messageId: plan.messageId,
  surfaceInstanceId: plan.surface.surfaceInstanceId,
  componentId: 'choice',
  eventType: 'change',
  valueToken: 'temporary-token',
});
```

事件必须携带完整 owner。Token 只表示同一组件、同一 surface instance 内的相等关系，不得包含原始值或跨组件复用。

真实业务 action 发生前，Host 应调用 `session.suspend()`。surface 被替换、删除或页面离开时调用 `handle/session.deactivate()`；应用卸载时调用 `runtime.dispose()`。

## 6. 安全执行更新建议

```ts
import { ValidatedSuggestionExecutor } from '@openjiuwen/iiap';

const executor = new ValidatedSuggestionExecutor((updates) => {
  applyDataModelBatchAtomically(updates);
});
```

用户接受 `update_suggestion` 后，用原 packet 的 `allowedOperations.updateTargets` 构造 `SuggestionContext` 再执行。任一目标、值、surface owner 或集合基数非法时，整批拒绝。

## 7. 接入 Python 决策服务

```python
from iiap import DecisionService

service = DecisionService(model_adapter)
envelope = await service.decide(packet)
```

`DecisionService` 会先按 wheel 内置的同版 Schema 校验完整 packet，再构造模型请求。格式错误的 packet 抛出 `ValueError("INVALID_PACKET")`，不会进入模型。

HTTP/RPC Handler 仍负责身份认证、请求大小限制、基础设施超时、追踪，以及把稳定错误映射为传输层响应。

## 8. 下一步

- 查看所有可运行示例：[examples/README_zh.md](../../examples/README_zh.md)
- 查询稳定接口：[API 参考](api-reference.md)
- 理解隐私与更新边界：[协议与安全](protocol-and-security.md)
- 执行完整验证：[测试与兼容性](testing-and-compatibility.md)
