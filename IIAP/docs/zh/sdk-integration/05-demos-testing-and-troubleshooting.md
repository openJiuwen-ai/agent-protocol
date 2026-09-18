# Demo、测试与故障排查

最后更新：2026-09-18

## 1. 示例地图

| 示例 | 覆盖内容 | 是否可运行 |
|---|---|---|
| [`examples/typescript/complete-v08.ts`](../../../examples/typescript/complete-v08.ts) | A2UI v0.8 Adapter、Runtime、Transport、Presenter、Assistance、更新执行入口 | 是；首选学习入口 |
| [`examples/python/complete_service.py`](../../../examples/python/complete_service.py) | ModelAdapter、DecisionService、AssistanceService、关联 Envelope | 是；首选服务端入口 |
| [`examples/browser/`](../../../examples/browser/) | 默认 React Presenter 的接受/拒绝 UI | 可 production build |
| [`examples/typescript/minimal.ts`](../../../examples/typescript/minimal.ts) | Adapter、Runtime、Session、Handle 最小生命周期 | 是；不包含决策闭环 |
| [`examples/python/minimal.py`](../../../examples/python/minimal.py) | DecisionService 最小函数封装 | 代码片段 |

完整示例使用当前已验证的 A2UI v0.8 路径。v0.9.1 只保留未发布的源码 prototype，没有公共 npm 入口，也不提供 conformance Demo。

## 2. 运行完整 Demo

从 `IIAP/` 根目录执行：

```bash
npm run build
node implementations/typescript/dist/examples/typescript/complete-v08.js

PYTHONPATH=implementations/python/src \
  uv run python examples/python/complete_service.py

npm run demo:build
```

TypeScript Demo 的预期输出：

```text
offer: decision-demo-1 需要我帮你比较这些选项吗？
assistance: 可以先按预算和使用频率比较，再选择最合适的选项。
```

Python Demo 会打印相关联的 `iiap.decision` 和 `iiap.assistance.response`。示例不启动 HTTP 服务、不调用远程模型，也不修改真实业务数据；它用于证明接口如何连接，而不是证明模型质量。

## 3. TypeScript Demo 逐段说明

### 3.1 `DemoTransport`：展示 Transport 契约

```ts
class DemoTransport implements DecisionTransport {
  async decide(packet) {
    return {
      type: 'iiap.decision',
      iiapVersion: '0.1',
      decisionId: 'decision-demo-1',
      packetId: packet.packetId,
      surfaceInstanceId: packet.surfaceInstanceId,
      payload: { /* offer_help */ },
    };
  }

  async assist(request) {
    return {
      type: 'iiap.assistance.response',
      iiapVersion: '0.1',
      requestId: request.requestId,
      message: '纯文字帮助',
    };
  }
}
```

Demo 用内存实现替代网络。客户生产代码有两种选择：直接使用 `HTTPDecisionTransport`；或者让现有 WebSocket/RPC 实现相同的 `DecisionTransport`。无论哪种方式，response 必须复用请求中的 packet/surface/request Owner。

### 3.2 `AutoAcceptPresenter`：展示 Presenter 契约

```ts
class AutoAcceptPresenter implements HelpPresenter {
  async present(decision, context) {
    console.log('offer:', context.decisionId, decision.message);
    return 'accepted';
  }
}
```

它为了确定性测试自动接受。真实 React 项目应使用 `ReactHelpPresenter + IIAPHelpHost`；非 React 项目实现自己的 UI，并在用户操作后完成 Promise。不要在 `present()` 内直接调用模型或修改 data model。

### 3.3 Adapter 与更新授权

```ts
const adapter = new A2UIV08Adapter({
  resolveAllowedValues: ({ declaredValues }) => declaredValues,
});
```

`A2UIV08Adapter` 负责把最终 v0.8 message 变成 `ObservationPlan`。Demo 为了展示接口，显式批准组件声明的有限值；生产 `SuggestionPolicy` 应根据业务字段逐项授权，不能对全部 `declaredValues` 无条件放行。

```ts
const executor = new ValidatedSuggestionExecutor(async (updates) => {
  console.log('apply safe data-model update batch:', updates);
});
```

SDK 负责在执行前再次校验 accepted、surface、path、批次结构、多选上限和 allowedValues；客户提供的回调负责把完整数组原子写入自己的 A2UI store。Demo 只打印，不修改数据。

### 3.4 Runtime 与 `onAccept`

```ts
const runtime = createIIAPRuntime({
  transport,
  presenter,
  policy: {
    minReportIntervalMs: 0,
    thresholds: { optionChangeCount: 1 },
  },
  onAccept: async (decision, context) => {
    if (decision.offerType === 'text_assistance') {
      await transport.assist(/* AssistanceRequest */);
      return;
    }
    if (decision.updateSuggestion) {
      await executor.execute(decision.updateSuggestion, {
        surfaceInstanceId: context.packet.surfaceInstanceId,
        allowedTargets: context.packet.allowedOperations.updateTargets,
        accepted: true,
      });
    }
  },
});
```

Demo 把阈值降到一次变化，只为了每次运行都能触发。生产应使用 production policy。`onAccept` 是唯一执行入口：文字帮助在这里创建 `AssistanceRequest`；UI 更新在这里调用安全 Executor。用户拒绝、忽略或超时时不会调用它。

### 3.5 Message、Plan 与 Handle

Demo 使用 v0.8 `surfaceUpdate + beginRendering` 构建一个单选 surface：

```ts
const plans = adapter.buildObservationPlans({
  sessionId: 'session-demo',
  messageId: 'message-demo',
  namespace: 'message-demo',
  messages: finalA2UIV08Messages,
});

const session = runtime.createSession({ sessionId: 'session-demo' });
const handle = session.activate(plans[0], { focused: true });
```

`namespace` 与 message 实例绑定，因此得到稳定的 `message-demo:preferences`。实际系统应保存每个 surface 的 Handle，不能每次 render 都创建随机 namespace。

### 3.6 Renderer 事件与触发

```ts
handle.observe({
  messageId: 'message-demo',
  surfaceInstanceId: 'message-demo:preferences',
  componentId: 'meal-type',
  eventType: 'change',
  valueToken: 'option-1',
  selectionState: 'selected',
});

await handle.flush();
```

Owner 三元组必须与 plan 一致。`valueToken` 只表达同一组件内的状态关系，不能替换成真实选项值。Demo 显式 `flush()`；生产通常等待 quiet/idle deadline。

### 3.7 清理

```ts
session.deactivate('session-ended');
runtime.dispose();
```

真实应用还应在 surface 删除时释放对应 Handle，在真实用户新 turn 时 `session.suspend()`，在应用卸载时 `runtime.dispose()`。

## 4. Python Demo 逐段说明

### 4.1 实现 `ModelAdapter`

```python
class DemoModelAdapter:
    async def generate_decision(self, request: ModelRequest) -> object:
        return {"decision": "offer_help", ...}

    async def generate_assistance(self, request: ModelRequest) -> object:
        return {
            "type": "iiap.assistance.response",
            "iiapVersion": "0.1",
            "requestId": request.payload["requestId"],
            "message": "纯文字帮助",
        }
```

客户把这里替换成真实业务 Agent 或模型 SDK。ModelAdapter 只生成业务 payload；`decisionId`、`packetId` 和 `surfaceInstanceId` 由 Service 使用可信请求构造。

### 4.2 调用 Service

```python
model = DemoModelAdapter()
decision = await DecisionService(model).decide(packet)
assistance = await AssistanceService(model).assist(request)
```

`DecisionService` 负责 prompt、隐私检查、payload 校验和 Envelope；`AssistanceService` 负责 request 校验、请求级 prompt 和纯文本校验。HTTP/FastAPI/Flask 不是 SDK 强制依赖，客户只需把自己的 `/decision`、`/assistance` 入口连接到这两个异步方法。

## 5. 最小端到端验收

至少准备两个 fixture：一个只允许文字帮助，一个显式授权批量 UI 更新并同时覆盖单选与多选集合。

文字帮助场景：

1. 创建 A2UI 表单并激活 ObservationPlan。
2. 连续产生足以形成模式的脱敏事件。
3. 确认 packet 包含当前窗口、此前最多 3 个报告窗口和有效 UI 定义，但不包含原始数据值。
4. 返回文字帮助 decision，确认出现非阻塞 Offer。
5. dismissed 后不调用 assistance；accepted 后通过内部 turn 获得纯文本 assistant message，界面不出现伪造的 user message。
6. IIAP assistance message 不应让原 surface 停止观察；真实用户新 turn 才 suspend 当前关注。

UI 更新场景：

1. Host 为可更新字段显式配置有限 `allowedValues`。`maxAllowedSelections` 是**可选的形状约束**，不是授权前置条件：需要约束多选值上限时声明正整数 `1`（单选）或 `≥2`（多选），不声明时建议值退化为「白名单内的去重子集」。不配置 `SuggestionPolicy` 等于没有更新权限。
2. 在测试配置中立即 `flush()` 或使用确定性决策，避免依赖模型自然触发概率。
3. accepted 前 data model 不发生变化。
4. accepted 后以一个批次更新白名单中的一个或多个字段并记录 succeeded；多选值是完整选择集合。任一非法值、重复 path、超上限集合或 stale surface 必须让整批拒绝并记录 failed。

Host 测试控制台选择的确定性场景必须附加到同一个实际发往 DecisionService 的 packet，不能只保存在本地诊断副本中。`update_suggestion` 只有在 packet 含合法 `allowedOperations.updateTargets` 时才能生成或执行更新；没有授权目标时必须在触发前报告，不能悄悄改成其他文字帮助。测试模式与生产模式使用同一套授权规则：绑定到 data model 且静态声明了有限取值的字段都可以成为更新 target，`maxAllowedSelections` 只约束多选值的形状，不再是授权前置条件。production profile 不携带 `testScenario`，也不改写模型的正常决策结果。

当 test profile 的模型没有返回合法更新时，JiuwenSwarm 的确定性 fallback 会从授权目标中选取最多两个不同字段；多选字段使用不超过声明上限的完整集合。fallback 的卡片文案必须从最终结构化更新生成并逐项列出字段和值，不能沿用模型的其他场景文案。该 fallback 仅由 `testScenario=update_suggestion` 启用，production profile 不使用它。

多 surface 场景还需确认：历史 surface 保持注册；只有 focused surface 自动超时；操作历史 surface 后焦点转移；两个 packet 倒序返回时，迟到 decision 不展示也不执行。

## 6. 验证命令

SDK 自检与 Demo：

```bash
cd <path-to-IIAP>
npm test
npm run packages:smoke
node implementations/typescript/dist/examples/typescript/complete-v08.js
PYTHONPATH=implementations/python/src uv run python examples/python/complete_service.py
uv run --with jsonschema python scripts/check_contracts.py
uv run python scripts/check_docs.py
npm run demo:build

cd implementations/python
PYTHONPATH=src uv run --with jsonschema --with pytest --with pytest-asyncio \
  pytest -q -c pyproject.toml tests
```

`npm run packages:smoke` 是制品发布门禁，不是源码 import smoke：它把 npm tarball
安装到全新临时 npm 项目，把 Python wheel 安装到全新虚拟环境，再从两个安装环境按
正式包名导入。门禁同时检查 tarball 中的 `README.md`、许可证、类型声明和受支持
子入口。Python 源码测试仍由后面的 pytest 命令单独覆盖。

JiuwenSwarm 参考回接：

```bash
cd <path-to-jiuwenswarm>
uv run pytest -q tests/unit_tests/iiap tests/unit_tests/a2ui \
  tests/system_tests/test_a2ui_system_flow.py

cd jiuwenswarm/channels/web/frontend
npm run test:iiap
npx tsc --noEmit
npm run build
```

### 6.1 源码候选验证记录

2026-09-18 在当前源码候选上完成了以下验证：

| 范围 | 结果 |
|---|---|
| JSON Schema 与共享 fixtures | 7 组通过 |
| TypeScript SDK clean build/test | 43 项通过 |
| Python SDK test/build | 16 项通过；sdist 与 wheel 构建成功 |
| 完整 TypeScript/Python Demo | 构建并运行通过 |
| SDK 文档检查 | 21 篇通过 |
| JiuwenSwarm IIAP/A2UI focused tests | 97 项通过 |
| 不依赖远程模型的 A2UI system test | 1 项通过 |
| 前端 IIAP 测试与 TypeScript typecheck | 45 项通过；typecheck 通过 |
| 无凭据集成链路与场景序列 | 22 项 + 12 项通过 |
| JiuwenSwarm production build | 通过 |
| `@openjiuwen/iiap` tarball clean install | 通过 |
| Python wheel clean install | 通过 |
| Browser Demo production build | 通过 |

上表是指定日期的源码候选快照；任何后续提交都必须重新运行第 6 节命令，
其中 npm tarball/Python wheel 的结果以 `npm run packages:smoke` 当次退出码为准。
这些结果不包含 A2UI v0.9.1 官方 conformance，也不能替代客户模型质量、业务文案、移动端和无障碍验收。

## 7. 故障排查

| 现象 | 优先检查 | 预期处理 |
|---|---|---|
| 没有活跃的可观察 surface | Adapter 是否产生 plan；Host 是否过早 deactivate；namespace 是否稳定 | 保留仍可交互的历史注册，只在删除/替换/会话结束时终止 |
| v0.9.1 message 得不到 plan | 是否误用了未发布的源码 prototype | v0.1 没有 v0.9.1 公共 Adapter；应等待正式实现或写经自有 schema 验证的 Custom Adapter |
| 有交互但没有 packet | capability、Owner、阈值、quiet/idle、cooldown、退避和 focused 状态 | 用诊断回调确认 rejected 原因；测试时可受控 `flush()` |
| 历史 surface 自动频繁触发 | Host 是否为每个 surface 自建 timer | 只让 Runtime 的 focused surface 使用自动 deadline |
| packet 被拒绝 | forbidden key、大小上限、Schema version 和 redaction | 修复采集或 Adapter，不放宽隐私校验 |
| decision 没有展示 | Envelope 关联、surface 是否 stale、Presenter 实例、Offer TTL | 丢弃未知、错属、重复或迟到结果 |
| 接受后出现 user message | Host 把内部 AssistanceRequest 当成普通用户 turn | 使用隐藏内部 turn；只展示校验后的 assistant message |
| assistance 没有显示 | 纯文本 validator、A2UI 内容、请求关联或模型异常 | 失败关闭，不重试、不写 history；记录 accepted/failed |
| UI 建议未执行 | 是否 accepted、allowedValues、多选上限、批次重复 path、surface/path/value 二次校验 | 任一越界或 stale 更新必须让整批拒绝 |
| 帮助卡相互覆盖 | 多 Runtime 是否复用 Presenter；旧 decisionId 是否响应新 Offer | 每个 Presenter 实例最多一个 Offer；替换旧 Offer 返回 ignored |

## 8. 自动化通过后仍需人工确认

- 帮助文案是否符合客户业务语境且不过度打扰。
- 业务 Agent 与独立模型两种路由所需上下文是否充足。
- 客户自定义组件的 capability 映射是否准确。
- 远端 feedback 上传是否满足合规和数据保留要求。
- 移动端或非 React Presenter 的交互与可访问性是否达到产品标准。
