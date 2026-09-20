# 快速开始

最后更新：2026-09-20

## 1. 构建

要求：Node.js 20+、Python 3.11+。从 IIAP/ 执行：

~~~bash
npm ci
npm run build
uv sync --project implementations/python --extra test
~~~

当前版本尚未发布到公共 registry。生产接入应固定源码提交，并使用
npm run packages:smoke 验证过的 npm tarball 和 Python wheel。

## 2. 把 A2UI 转成 ObservationPlan

~~~ts
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
~~~

必须传入已经完成校验、修复并交给 Renderer 的 final message，不能观察模型流式草稿。
没有 Host SuggestionPolicy 时 Adapter 不授权任何 UI 更新。

## 3. 选择一种 Runtime 模式

### 3.1 Host 托管

适用于已有 WebSocket、RPC、消息总线或自定义 UI 的系统：

~~~ts
import { createIIAPRuntime } from '@openjiuwen/iiap';

const runtime = createIIAPRuntime({
  onPacket: async (packet) => sendThroughExistingChannel(packet),
  presenter: hostPresenter,
  onAccept: applyAcceptedHelp,
});
~~~

Host 收到服务端 envelope 后调用 session.handleDecision(envelope)；外部请求取消时调用
session.cancelDecision(packetId)。用户反馈可由 Presenter Promise 返回，或通过
session.recordFeedback(feedback) 录入。

### 3.2 SDK 托管

适用于使用标准请求/响应 Transport 的系统：

~~~ts
import { createIIAPRuntime } from '@openjiuwen/iiap';
import { HTTPDecisionTransport } from '@openjiuwen/iiap/http';

const runtime = createIIAPRuntime({
  transport: new HTTPDecisionTransport({ baseUrl: '/api/iiap' }),
  presenter,
  feedbackUpload: true,
  onAccept: applyAcceptedHelp,
});
~~~

transport 与 onPacket 互斥；同时配置会立即抛出配置错误。是否上传反馈只由
Runtime 的 feedbackUpload 控制。

## 4. 激活并观察

~~~ts
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
~~~

事件必须携带完整 owner。Token 只表示同一组件、同一 surface instance 内的相等关系，
不得包含原始值或跨组件复用。

真实业务 action 发生前，Host 应调用 session.suspend()。surface 被替换、删除或页面离开时，
调用 handle/session 的 deactivate()；应用卸载时调用 runtime.dispose()。

## 5. 安全执行更新

~~~ts
import { ValidatedSuggestionExecutor } from '@openjiuwen/iiap';

const executor = new ValidatedSuggestionExecutor((updates) => {
  applyDataModelBatchAtomically(updates);
});
~~~

用户接受 update_suggestion 后，用原 packet 的 allowedOperations.updateTargets 构造
SuggestionContext 再执行。任一目标、值、surface owner 或集合基数非法时，整批拒绝。

完整可运行示例见 examples/typescript/complete-v08.ts 和
examples/python/complete_service.py。
