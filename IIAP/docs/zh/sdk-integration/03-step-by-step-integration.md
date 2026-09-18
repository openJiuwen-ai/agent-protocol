# 分步接入

最后更新：2026-09-18

本页按“先建立观察，再闭环帮助，最后开放安全更新”的顺序实施。不要一开始同时替换业务 Agent、A2UI 生成链路和 IIAP 接入。

## 0. 先确定宿主中的七个连接点

在安装依赖之前，先在现有系统中找到下列位置。它们可以位于同一个应用，也可以跨端侧和服务端部署；模块名不要求一致，但职责必须能对应。

| 连接点 | 在现有系统中寻找什么 | 接入后调用什么 |
|---|---|---|
| 会话生命周期 | conversation 创建、切换、恢复、结束和真实用户新 turn | `createSession`、`suspend`、`deactivate` |
| 最终 A2UI message | 已完成生成、校验、修复、降级并真正进入 Store 的消息 | `adapter.buildObservationPlans`、`session.activate` |
| Renderer 语义事件 | 组件 change、validation、open、tab、media、scroll 等事件处理器 | 对应 surface 的 `handle.observe` |
| 端侧网络调用 | 已有 HTTP、RPC 或 WebSocket client | 默认 `HTTPDecisionTransport` 或自定义 `DecisionTransport` |
| 帮助展示 | 可展示轻量提示、接受/拒绝，以及 assistant assistance 的 UI | 默认 `ReactHelpPresenter` 或自定义 `HelpPresenter`；`onAccept` 连接消息层 |
| 服务端模型入口 | 已有 API handler、Agent router 或模型 gateway | `DecisionService`、`AssistanceService`、客户 `ModelAdapter` |
| 可选 data-model 写入 | Store 中经过业务校验的原子批量更新入口 | `SuggestionPolicy`、`ValidatedSuggestionExecutor(applyBatch)` |

业务意图识别、A2UI 生成、Schema 校验/修复、协议降级和流式传输的原有控制流程不需要改写。完整关系图见[端到端架构、公共边界与系统插入点](01-end-to-end-architecture-and-boundaries.md)。如果无法为某个连接点找到 owner，应先补齐 Host 责任，不要把内部 IIAP 模块暴露出来绕过该边界。

## 1. 从 GitCode 源码发布安装 SDK

npm 和 pip 是安装客户端，不等同于 npm registry 或 PyPI。只要 GitCode 中存在经过版本化的 IIAP 源码，客户仍可使用 npm 安装本地 `.tgz`，并使用 pip 安装 wheel、本地源码或 VCS 源码。

以下命令以正式源码 tag `iiap-v0.1.0` 和目标目录 `agent-protocol/IIAP/` 为例。发布前应以实际 tag 和校验值替换示例；不要在生产环境跟随可变的 `main` 分支。

### 1.1 获取并验证固定版本源码

```bash
git clone --branch iiap-v0.1.0 --depth 1 \
  https://gitcode.com/openJiuwen/agent-protocol.git
cd agent-protocol/IIAP
```

如果客户已通过内部镜像或源码压缩包获得 `agent-protocol`，只需进入其中的 `IIAP/` 目录。随后验证源码：

```bash
npm ci
npm test
uv run --with jsonschema python scripts/check_contracts.py

cd implementations/python
PYTHONPATH=src uv run --with jsonschema --with pytest --with pytest-asyncio \
  pytest -q -c pyproject.toml tests
```

### 1.2 TypeScript：构建 tarball 后使用 npm 安装

在 `IIAP/` 根目录生成标准 npm tarball：

```bash
npm run build
mkdir -p build/npm
npm pack ./implementations/typescript/packages/core --pack-destination ./build/npm
```

产物为 `build/npm/openjiuwen-iiap-0.1.0.tgz`。把它复制到客户项目的 `vendor/` 目录或上传到客户内部制品库，然后在客户前端项目中安装：

```bash
cd <customer-frontend>
npm install ./vendor/openjiuwen-iiap-0.1.0.tgz
```

客户代码继续使用稳定包名：

```ts
import { createIIAPRuntime } from '@openjiuwen/iiap';
import { A2UIV08Adapter } from '@openjiuwen/iiap/a2ui-v08';
```

不要直接执行 `npm install git+https://gitcode.com/openJiuwen/agent-protocol.git#iiap-v0.1.0`。标准 npm Git dependency 会把仓库根目录视为 package 根目录，不能可靠选择当前 monorepo 中的 `IIAP/implementations/typescript/packages/core`，而且源码 tag 中的 TypeScript 还需要先生成 `dist/`。

### 1.3 Python：使用 pip 安装源码或 wheel

如果安装环境能够访问 GitCode、具备 Git 和 Python 构建工具，可以直接固定 tag 安装 Python 子目录：

```bash
python -m pip install \
  "openjiuwen-iiap @ git+https://gitcode.com/openJiuwen/agent-protocol.git@iiap-v0.1.0#subdirectory=IIAP/implementations/python"
```

生产、离线或多实例部署更推荐先构建一次 wheel：

```bash
cd <path-to-agent-protocol>/IIAP/implementations/python
python -m pip install build
python -m build --outdir ../../build/python
```

把生成的 wheel 复制到客户后端项目的 `vendor/` 目录或内部 Python 制品库，然后安装：

```bash
cd <customer-backend>
python -m pip install ./vendor/openjiuwen_iiap-0.1.0-py3-none-any.whl
```

单机开发也可以从已 clone 的源码目录直接安装：

```bash
python -m pip install <path-to-agent-protocol>/IIAP/implementations/python
```

安装完成后，三种方式都使用相同 import：

```python
from iiap import AssistanceService, DecisionService
```

### 1.4 版本固定与制品管理

- GitCode tag 必须指向不可变的已评审 commit；客户安装记录同时保存 tag 和 commit SHA。
- `.tgz`、wheel 和源码 tag 必须具有相同的 IIAP 版本号，并由 CI 运行相同 contract/test。
- 生产环境应保存构建产物及其 SHA-256，避免每台机器分别从源码构建。
- 可以把产物上传到客户内部 npm/Python registry；这不会改变公共包名、import 或 API。
- 如果未来发布公共 npm/PyPI，客户只需替换依赖获取方式，不需要修改业务代码。

当前本地源码候选也可以采用完全相同的产物构建方式：

```bash
cd <path-to-IIAP>
npm ci
npm run packages:smoke
```

`packages:smoke` 使用系统临时目录构建并安装 npm tarball 和 Python wheel，随后从全新
consumer/virtual environment 按正式包名导入；成功后会自动清理临时目录。它是发布
门禁，不保留交付制品。需要保存制品时，再分别运行上文的 `npm pack` 与 `uv build`，
并保存 SHA-256。

## 2. 建立 A2UI 消息到 ObservationPlan 的映射

选择与实际 A2UI wire version 对应且已经完成 conformance 的 Adapter。当前可直接用于生产接入的只有 v0.8；v0.9.1 不提供公共 npm 入口。必须传入最终交付给 Renderer 的 message，而不是降级、修复之前的草稿。

```ts
import { A2UIV08Adapter } from '@openjiuwen/iiap/a2ui-v08';

const adapter = new A2UIV08Adapter();
const plans = adapter.buildObservationPlans({
  sessionId,
  messageId,
  namespace: messageId,
  messages: finalA2UIMessages,
});
```

接入官方 v0.9.1 系统时，当前应实现经过客户 schema 验证的 Custom Adapter，或等待后续版本提供已完成官方扁平组件和 catalog conformance 的公共 Adapter；不得 deep import 当前未发布的源码 prototype。

`namespace` 应来自稳定的消息实例，不应使用每次 render 都变化的时间戳或随机 DOM ID。Adapter 会组合 namespace 与协议 surface ID，形成当前消息范围内稳定的 `surfaceInstanceId`。

## 3. 创建 Runtime 并管理历史 surface

```ts
import { createIIAPRuntime } from '@openjiuwen/iiap';

const runtime = createIIAPRuntime({ transport, presenter, onAccept, onError });
const session = runtime.createSession({ sessionId });

for (const [index, plan] of plans.entries()) {
  const handle = session.activate(plan, { focused: index === plans.length - 1 });
  surfaceHandles.set(plan.surface.surfaceInstanceId, handle);
}
```

Host 应保留会话历史中仍可交互的 surface 注册。最新 surface 默认 focused；用户操作历史 surface 时，将事件发给它原有的 handle，Runtime 会转移关注权。真实用户发起新对话或触发明确业务 action 时调用 `session.suspend()`；surface 删除/替换时调用对应 `handle.deactivate(...)`；会话结束时调用 `session.deactivate(...)`，应用卸载时调用 `runtime.dispose()`。

## 4. 接入 Renderer 语义事件

在 Renderer 自己的 change、validation、open/close、tab、media、scroll 和 explicit action 入口构造事件：

```ts
handle.observe({
  messageId,
  surfaceInstanceId,
  componentId,
  eventType: 'change',
  optionToken: tokenForOption(componentId, selectedOption),
  selectionState: 'selected',
});
```

token 必须是短期、不可反推的局部标识，不得传输输入文本、真实选项值、用户身份或业务对象。事件的 `messageId + surfaceInstanceId + componentId` 必须来自 ObservationPlan；Owner 缺失或不匹配时应拒绝，而不是回退到当前焦点 surface。

## 5. 接入决策服务

简单 HTTP 部署可以直接使用默认 Transport：

```ts
import { HTTPDecisionTransport } from '@openjiuwen/iiap/http';

const transport = new HTTPDecisionTransport({
  baseUrl: '/api/iiap',
  timeoutMs: 10_000,
  headers: { authorization: `Bearer ${accessToken}` },
  feedbackUpload: false,
});
```

服务端分别把 `/decision` 和 `/assistance` 连接到 Python `DecisionService`、`AssistanceService`。如果复用 WebSocket/RPC，只需实现同一个 `DecisionTransport` 接口。Decision 响应必须由服务端使用原 packet 生成关联 ID；不能要求模型生成 `packetId`、`decisionId` 或 `surfaceInstanceId`。

## 6. 展示帮助并处理用户响应

React 宿主可以使用默认 Presenter：

```tsx
const presenter = new ReactHelpPresenter({ ttlMs: 60_000 });
root.render(<IIAPHelpHost presenter={presenter} />);
```

Runtime 和 `IIAPHelpHost` 必须绑定同一个 Presenter 实例。用户拒绝、关闭、超时或 Offer 被替换时，不得触发 assistance 或 UI 更新。

接受文字帮助后，Host 通过内部 turn 调用 `transport.assist` 或业务 Agent：

- 请求对模型可见，但不添加前端 user message，也不写入 user history；
- IIAP 请求级 prompt 只作用于本轮；
- 完整输出缓冲并通过纯文本校验后，才显示为 assistant assistance message；
- 发现 A2UI message 或 tagged A2UI 输出时失败关闭：不重试、不展示、不写 history。

## 7. 可选：开放安全 UI 更新

先完成文字帮助闭环，再按业务字段配置 `SuggestionPolicy`。模型只能在 packet 的 `allowedOperations.updateTargets` 中选择值；客户端 Adapter 仍需再次执行 `validateSuggestion`，并在确认当前 `surfaceInstanceId` 未过期后交给 Executor。

```ts
const safe = adapter.validateSuggestion(decision.updateSuggestion, {
  surfaceInstanceId,
  allowedTargets: packet.allowedOperations.updateTargets,
  accepted: true,
});
if (safe) await executor.execute(safe, context);
```

没有明确白名单、用户没有接受、surface 已失效，或多选字段缺少显式上限时，一律不执行更新。一次建议可以包含多个字段；多选值使用完整集合，任一字段非法则整批拒绝。具体策略见[自定义 Adapter、配置与安全](04-custom-adapters-configuration-and-security.md)。

## 8. 记录反馈并验收

Presenter 终态记录 `interaction`；只有 accepted 后的 assistance/Executor 完成才记录 `outcome: succeeded|failed`。本地反馈始终驱动退避；远端上传默认关闭，需要完成合规评审后显式启用。

完成后按[Demo、测试与故障排查](05-demos-testing-and-troubleshooting.md)运行最小闭环和异常场景，不要仅以“出现帮助弹窗”作为接入成功标准。
