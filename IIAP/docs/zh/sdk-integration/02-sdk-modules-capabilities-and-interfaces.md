# SDK 模块、接口与数据结构开发参考

最后更新：2026-09-18

本文是 IIAP SDK 公共模块、TypeScript/Python API 和关键数据结构用法的唯一人类可读参考。阅读顺序遵循真实调用链，而不是编程语言：先建立 UI 观察，再连接决策服务，最后展示或执行帮助。端到端插入位置见[端到端架构、公共边界与系统插入点](01-end-to-end-architecture-and-boundaries.md)；跨进程字段、关联规则和 JSON 示例见[Wire 协议与数据契约](../sdk-design/02-wire-protocol-and-data-contracts.md)，机器可执行约束以 `IIAP/contracts/schemas/` 为准。

## 1. 阅读方式与模块索引

### 1.1 采用方式

| 标识 | 含义 | 客户动作 |
|---|---|---|
| 直接调用 | SDK 提供完整实现，也是正常接入入口 | import 后创建或调用 |
| 默认实现 | SDK 提供通用实现，也允许替换 | 满足部署条件时直接采用 |
| 客户实现 | SDK 只定义扩展端口，能力依赖客户系统 | 实现接口并注入 SDK |
| 进阶工具 | 自定义边界、诊断或测试才需要 | 普通接入可以忽略 |
| SDK 内部 | Runtime/Service 的实现细节 | 不 import、不复制状态机 |

接口“被导出”不表示每个客户都要调用。DTO 因出现在公共方法签名中而必须可见；Adapter、Transport 等 SPI 只有在默认实现不适用时才需要客户实现。

本文对每个 API 使用同一说明标准：**接口能力、调用关系、签名、输入、返回与副作用、失败行为、最小示例**。没有入参、返回值或特定异常的简单方法也会明确写出“无”，不通过省略表达。

### 1.2 模块快速索引

| 模块 | 解决的问题 | 典型使用者 | 采用方式 | import/入口 |
|---|---|---|---|---|
| [IIAP Runtime](#2-iiap-runtime-模块) | 管理会话、surface、观察、触发、关联和反馈 | UI 侧 Host | 直接调用 | `@openjiuwen/iiap` |
| [UI Protocol Adapter](#3-ui-protocol-adapter-模块) | 把实际 UI 协议转换成 IIAP 可观察模型 | A2UI Parser/Store 集成层 | v0.8 默认实现；其他协议客户实现 | `@openjiuwen/iiap/a2ui-v08` |
| [Decision Transport](#4-decision-transport-模块) | 在 Runtime 与服务端之间传输 decision/assistance | 端侧网络层 | HTTP 默认实现；其他通道客户实现 | `@openjiuwen/iiap/http` |
| [Help Presenter](#5-help-presenter-模块) | 展示帮助入口并采集接受、拒绝或超时 | 应用界面层 | React 默认实现；其他 UI 客户实现 | `@openjiuwen/iiap/react` |
| [Safe Suggestion](#6-safe-suggestion-模块) | 授权、校验并执行用户接受的安全 UI 更新 | A2UI Data Model Store | 可选；策略和写入由客户提供 | `@openjiuwen/iiap` |
| [Decision/Assistance Service](#7-decisionassistance-service-模块) | 构造请求级 Prompt、调用模型并校验输出 | 服务端 API | 直接调用 | Python `iiap` |
| [Model Integration](#8-model-integration-模块) | 把 Service 接到业务 Agent、独立模型或模型网关 | 模型/Agent 集成层 | `ModelAdapter` 客户实现 | Python `iiap` |
| [Contract、Validator 与测试](#9-contractvalidator-与测试工具) | 校验跨进程对象并支持自定义边界和测试 | API 网关、CI、测试 | 进阶工具 | `contracts/`、`/decision`、`/testing` |

Observation Engine、Pattern Aggregator、Policy Engine、Privacy Pipeline、Correlation/Feedback Controller 和 Prompt Builder 均由上述模块内部编排，没有独立客户接口。

### 1.3 包名与公共导出入口

`@openjiuwen/iiap` 是 TypeScript 的稳定逻辑包名，Python distribution 名为 `openjiuwen-iiap`、import 名为 `iiap`。包名不等于公共 registry 已经发布；当前可用安装渠道和源码构建命令统一见[分步接入](03-step-by-step-integration.md#1-从-gitcode-源码发布安装-sdk)。

TypeScript 公共入口：

| import 入口 | 公共内容 | 典型使用场景 |
|---|---|---|
| `@openjiuwen/iiap` | Runtime/Session/Handle、公共 DTO 和 port、Policy 类型、安全 Validator、Executor、错误类型 | 所有 UI 侧接入使用 Runtime；其余能力按签名选用 |
| `@openjiuwen/iiap/decision` | `parseDecision`、`createDecisionEnvelope`、`parseDecisionEnvelope` | 自定义 Transport 或服务端封装 |
| `@openjiuwen/iiap/http` | `HTTPDecisionTransport` 与配置类型 | 使用默认 HTTP 通道 |
| `@openjiuwen/iiap/a2ui-v08` | `A2UIV08Adapter` 与迁移兼容消息类型 | A2UI v0.8 正式接入 |
| `@openjiuwen/iiap/react` | `ReactHelpPresenter`、`IIAPHelpHost` | React 应用采用默认帮助入口 |
| `@openjiuwen/iiap/testing` | `createManualClock` | 确定性单元测试 |

Python 根入口 `iiap` 公开：

| 类别 | 公共内容 | 典型使用场景 |
|---|---|---|
| Service | `DecisionService`、`AssistanceService` | 服务端标准接入 |
| Model Integration | `ModelAdapter`、`ModelRequest`、`AgentRouter` | 连接业务 Agent、模型或模型网关 |
| Contract/Validator | decision、privacy、assistance validator 和 envelope helper | 自定义 API 边界与测试 |

源码根入口和 package `exports` 是实际可导入符号的真源。本页解释这些符号的稳定用途；未列入公共入口的 aggregation、pattern、feedback controller、clock 和 prompt builder 等内部实现不能 deep import，也不构成兼容承诺。v0.1 不提供统一的 `HostAdapter.attach/detach`，Host 使用后续各模块定义的 port 编写少量集成代码。

## 2. IIAP Runtime 模块

### 2.1 模块概述

Runtime 是 UI 侧接入 IIAP 的根入口。它把应用会话、A2UI surface 和 Renderer 事件组织成相互隔离的状态，负责内部事件聚合、quiet/idle 触发、隐私裁剪、异步 decision 关联、帮助展示和本地反馈退避。

客户不需要直接操作 Observation、Pattern、Policy 或 Feedback Controller。客户只负责把真实生命周期和语义事件送入 Runtime，并配置 Transport、Presenter 和接受帮助后的 Host 行为。

### 2.2 在流程中的位置与采用方式

- **上游**：会话管理器、A2UI Store、Renderer；
- **下游**：Decision Transport、Help Presenter、`onAccept` Host callback；
- **采用方式**：所有 UI 侧接入都直接调用；SDK 提供完整实现；
- **实例关系**：一个应用 Runtime 可以创建多个 Session；一个 Session 可以持有多个历史 surface Handle。

```ts
import { createIIAPRuntime } from '@openjiuwen/iiap';
```

### 2.3 API 快速查找

| API | 能力 | 谁调用 | 谁实现 |
|---|---|---|---|
| `createIIAPRuntime(options?)` | 创建隔离的 IIAP 根实例并注入依赖 | 应用启动/IIAP 插件初始化代码 | SDK |
| `runtime.createSession(context)` | 为一个业务会话建立 IIAP 状态 | 会话管理器 | SDK |
| `runtime.dispose()` | 释放整个 Runtime | 应用卸载/插件关闭逻辑 | SDK |
| `session.activate(plan, options?)` | 注册一个可观察 surface | A2UI Parser/Store 集成层 | SDK |
| `session.focus(surfaceInstanceId)` | 显式转移自动 timer 关注权 | surface/历史会话管理器 | SDK |
| `session.suspend()` | 在真实用户新 turn 时暂停当前关注 | 会话输入或业务 action 入口 | SDK |
| `session.handleDecision(envelope)` | 接收自定义异步通道返回的 decision | WebSocket/RPC 集成层 | SDK |
| `session.cancelDecision(packetId)` | 释放失败或超时的自定义异步请求 | WebSocket/RPC 集成层 | SDK |
| `session.recordFeedback(feedback)` | 提交 Host 自定义的终态反馈 | 自定义 Presenter/执行层 | SDK |
| `session.deactivate(reason)` | 结束一个会话的全部观察 | 会话管理器 | SDK |
| `handle.observe(event)` | 输入一个 surface 的脱敏语义事件 | Renderer 事件处理器 | SDK |
| `handle.flush()` | 受控地立即检查一次上报条件 | 测试台或显式 Host 控制 | SDK |
| `handle.deactivate(reason)` | 释放单个 surface | A2UI Store 生命周期代码 | SDK |

### 2.4 `createIIAPRuntime(options?)`

**接口能力**：创建 Runtime，并把网络、展示、策略和 Host callback 注入同一个状态机。应用不应为同一功能路径创建第二套 Runtime。

**调用关系**：由应用启动、会话容器初始化或 IIAP 插件启用代码调用；SDK 实现。它不创建具体 conversation，后续由 `createSession()` 完成。

**签名**：`createIIAPRuntime(options?: RuntimeOptions): IIAPRuntime`

```ts
const runtime = createIIAPRuntime({
  transport,
  presenter,
  onAccept,
  onError,
});
```

**输入**：`RuntimeOptions`：

| 字段 | 类型 | 必填 | 来源/默认行为 | 作用与约束 |
|---|---|---:|---|---|
| `transport` | `DecisionTransport` | 条件必填 | Host 注入；无默认实例 | 标准请求/响应通道；使用 `onPacket` 时可省略 |
| `presenter` | `HelpPresenter` | 建议提供 | Host 注入 | 展示 `offer_help`；省略后帮助不会出现在 UI |
| `policy` | `PolicyOverrides` | 否 | production 默认策略 | 只覆盖公开阈值、deadline、限流、历史数和包大小；events≤128、reportHistory≤3 为不可放宽的 wire 硬上限，较大覆盖值会被钳制 |
| `onAccept` | callback | 接受帮助时必填 | Host 实现 | 用户接受后请求文字 assistance 或执行安全更新 |
| `onError` | `(error) => void` | 否 | 默认失败关闭 | 接收网络、展示和执行错误；日志不得记录敏感 payload |
| `onPacket` | `(packet) => void/Promise` | 否 | 无 | 复用 Host 异步 WebSocket/RPC 时发送 packet |
| `feedbackUpload` | `boolean` | 否 | `false` | 只有为 true 且 Transport 支持时才上传反馈 |
| `idFactory` | `() => string` | 否 | SDK 生成 | 主要用于确定性测试；生产 ID 必须足够唯一 |
| `clock` | Clock-like | 否 | 系统时钟 | 只建议在测试中注入 |
| `onEventRejected` | callback | 否 | 无 | 诊断 owner 缺失、错配或未知组件 |
| `onDecisionRejected` | callback | 否 | 无 | 诊断无效、未知、错属、过期或重复 decision |
| `onFeedbackRejected` | callback | 否 | 无 | 诊断无效、未知、错属或重复 feedback |
| `onFlushSkipped` | `(reason, context) => void` | 否 | 无 | 诊断 flush 被 focused、in-flight、反馈退避、报告间隔/上限、pattern、隐私或体积 gate 拒绝；不得记录 packet 或原始值 |

**返回与副作用**：返回新的 `IIAPRuntime`。实例随后拥有其 Session、timer 和 pending 状态；构造本身不发送网络请求。

**失败行为**：配置 `onPacket` 时应省略 `transport`；同时配置会让同一个 packet 走两条路径。运行期错误通过 `onError` 报告，并应失败关闭而不阻断原业务 UI。

**最小示例**：见本节上方代码；应用卸载时必须调用 `runtime.dispose()`。

### 2.5 `runtime.createSession(context)`

**接口能力**：为一个 conversation 建立独立的 surface、timer、pending decision 和反馈状态。

**调用关系**：由 Host 会话管理器在 conversation 创建或恢复时调用；SDK 实现。

**签名**：`runtime.createSession(context: SessionContext): IIAPSession`

**输入**：`context.sessionId` 必填，来自 Host 的业务会话 ID；同一 Runtime 内应稳定且唯一。

**返回与副作用**：返回新的 `IIAPSession`，并在 Runtime 内注册它。使用相同 ID 再次创建会终止旧 Session，因此不能把本方法当作无副作用的 getter。

**失败行为**：当前无专用异常；传入空或不稳定 ID 会破坏 Host 关联，必须在调用前由 Host 保证有效。

```ts
const session = runtime.createSession({ sessionId: conversation.id });
```

### 2.6 `runtime.dispose()`

**接口能力**：终止全部 Session，清理 timer、待处理 decision、Presenter 状态和观察窗口。

**调用关系**：由应用卸载或插件关闭代码调用；SDK 实现。

**签名**：`runtime.dispose(): void`

**输入**：无。

**返回与副作用**：返回 void；所有 Session 以 `runtime-disposed` 结束，timer 和 pending 状态失效。

**失败行为**：重复清理不提供新的业务结果；调用后继续使用旧 Session/Handle 属于 Host 生命周期错误。

```ts
runtime.dispose();
```

### 2.7 `session.activate(plan, options?)`

**接口能力**：把 Adapter 生成的单 surface `ObservationPlan` 注册到当前 Session，并返回专属 Handle。

**调用关系**：由 A2UI Parser/Store 集成层在最终 message 已生效且 surface 仍可交互时调用；SDK 实现。

**签名**：`session.activate(plan: ObservationPlan, options?: ActivationOptions): ObservationHandle`

**输入**：

| 参数 | 来源 | 说明 |
|---|---|---|
| `plan` | `UIProtocolAdapter.buildObservationPlans()` | 必须属于当前 session，且只描述一个 surface |
| `options.focused` | Host 生命周期策略 | 默认 true；false 表示注册但不启用自动 deadline |

**返回与副作用**：返回只属于该 surface instance 的 Handle，并注册/替换对应观察状态。默认获得焦点；`focused:false` 只注册、不启用自动 deadline。

**失败行为**：plan 与 Session ID 不同、surface owner 缺失或 component owner 不一致时抛 `Error`。

```ts
const handle = session.activate(plan, { focused: isLatestSurface });
```

### 2.8 `session.focus(surfaceInstanceId)`

**接口能力**：把 quiet/idle timer lease 显式转移到某个已注册 surface。

**调用关系**：由历史会话/surface 管理器在恢复页面或明确知道视觉关注对象时调用；SDK 实现。正常交互时 `observe()` 会自动转移焦点。

**签名**：`session.focus(surfaceInstanceId: string): void`

**输入**：已通过 `activate()` 注册的 `surfaceInstanceId`。

**返回与副作用**：返回 void；旧焦点 timer 被清理，新 surface 获得自动 deadline lease。

**失败行为**：未知 ID 不抛错且不产生效果。

```ts
session.focus(restoredSurfaceInstanceId);
```

### 2.9 `session.suspend()`

**接口能力**：在真实用户发起新聊天 turn 或明确业务 action 时，撤销当前 timer lease、清空当前窗口并使旧 pending 结果失效，同时保留历史 surface 注册。

**调用关系**：由 Host 用户输入提交或显式业务 action 入口调用；SDK 实现。IIAP 自身的 assistance turn 不得调用。

**签名**：`session.suspend(): void`

**输入**：无。

**返回与副作用**：返回 void；当前焦点、窗口、timer、pending/presented owner 失效，但已注册历史 surface 保留。

**失败行为**：没有焦点时为无操作；错误地用于 IIAP 内部 turn 会造成业务语义错误，而不是抛异常。

```ts
onRealUserTurn(() => session.suspend());
```

### 2.10 `session.deactivate(reason)`

**接口能力**：结束当前业务会话的全部 surface 观察。会话切换但仍需恢复历史交互时，不应过早注销。

**调用关系**：由会话管理器在 conversation 真正结束时调用；SDK 实现。

**签名**：`session.deactivate(reason: DeactivationReason): void`

**输入**：结束原因，通常为 `session-ended` 或 `host-request`。

**返回与副作用**：返回 void；释放该 Session 的所有 Handle、timer、pending decision 和 offer。

**失败行为**：重复调用不产生新的结果；注销后旧 Handle 不应继续使用。

```ts
session.deactivate('session-ended');
```

### 2.11 `session.handleDecision(envelope)`

**接口能力**：把 `onPacket` 发出的 packet 对应 decision 送回 Runtime，让 Runtime统一执行 packet/surface owner、stale 和重复响应检查。

**调用关系**：由自定义 WebSocket/RPC 响应处理器调用；SDK 实现。标准 `transport.decide()` 路径无需调用。

**签名**：`session.handleDecision(envelope: IIAPDecisionEnvelope): boolean`

**输入**：服务端返回的未信任 decision envelope。

**返回与副作用**：true 表示响应被当前 pending packet 接受并进入后续展示；false 表示没有改变状态。

**失败行为**：无效、未知、错属、stale 或重复 decision 返回 false，并通过 `onDecisionRejected` 诊断，不抛给业务 UI。

```ts
if (!session.handleDecision(message.decision)) logStaleResponse();
```

### 2.12 `session.cancelDecision(packetId)`

**接口能力**：当自定义通道超时、解析失败或服务端明确拒绝时，释放指定 in-flight packet，允许后续报告继续产生。

**调用关系**：由自定义通道的 timeout/error handler 调用；SDK 实现。

**签名**：`session.cancelDecision(packetId: string): boolean`

**输入**：本 Session 先前通过 `onPacket` 发送的 packet ID。

**返回与副作用**：true 表示找到并释放 pending；false 表示没有匹配项。只影响指定 packet。

**失败行为**：未知或已完成 ID 返回 false，不抛错。

```ts
onRequestTimeout((packetId) => session.cancelDecision(packetId));
```

### 2.13 `session.recordFeedback(feedback)`

**接口能力**：提交自定义展示/执行路径产生的终态结果，让 Runtime 更新本地退避并进行重复/owner 检查。

**调用关系**：由完全自定义 Presenter/执行层调用；SDK 实现。标准 Presenter + `onAccept` 流程自动记录，不调用本方法。

**签名**：`session.recordFeedback(feedback: IIAPFeedback): boolean`

**输入**：与已展示 decision 的 packet、decision、surface 和 offerType 全部匹配的终态 feedback。

**返回与副作用**：true 表示终态被接受并更新本地退避；false 表示状态未改变。

**失败行为**：无效、未知、错属、offer 不匹配或重复 feedback 返回 false，并触发 `onFeedbackRejected`。

```ts
session.recordFeedback(feedbackEnvelope);
```

### 2.14 `handle.observe(event)`

**接口能力**：将 Renderer 中一次已经脱敏的语义交互加入该 surface 的观察窗口。它不返回“是否触发”，触发由内部策略异步判断。

**调用关系**：由 Renderer 的 change/validation/open/tab/media/scroll 等语义事件入口调用；SDK 实现。

**签名**：`handle.observe(event: ComponentEvent): void`

**输入**：已脱敏事件；`messageId + surfaceInstanceId + componentId` 必须与 Handle 的 plan 匹配。

**返回与副作用**：返回 void；合法事件加入窗口、可能转移焦点并重置 deadline。是否上报由内部策略决定。

**失败行为**：owner 缺失、错配或未知组件时拒绝事件并调用 `onEventRejected`，不抛错。

```ts
handle.observe(componentEvent);
```

### 2.15 `handle.flush()`

**接口能力**：立即运行一次与自动 timer 相同的上报判断，主要供测试控制台或明确的 Host 调试入口使用。

**调用关系**：由测试台、调试工具或显式 Host 控制调用；SDK 实现。生产正常流程依赖自动 deadline。

**签名**：`handle.flush(): Promise<IntentContextPacket | null>`

**输入**：无。

**返回与副作用**：达到上报条件时返回 packet 并进入 in-flight；否则返回 null。它不会绕过 policy。

**失败行为**：无可报告模式、处于退避、达到上限或已有请求时安全返回 null；配置 `onFlushSkipped` 后同时收到稳定 reason code 与不含原始值的 surface/automatic 上下文。传输错误走 Runtime `onError`。

```ts
const packet = await handle.flush();
```

### 2.16 `handle.deactivate(reason)`

**接口能力**：释放单个 surface 的 timer、窗口和 pending 状态。surface 删除或替换时调用；旧 Handle 之后不得再接收事件。

**调用关系**：由 A2UI Store 的 delete/replace 生命周期代码调用；SDK 实现。

**签名**：`handle.deactivate(reason: DeactivationReason): void`

**输入**：通常为 `surface-deleted`、`surface-replaced` 或 `host-request`。

**返回与副作用**：返回 void；只释放当前 Handle 对应 surface，不影响同 Session 的其他历史 surface。

**失败行为**：重复调用不产生新结果；注销后的事件不会恢复观察。

```ts
handle.deactivate('surface-deleted');
```

### 2.17 Runtime 相关数据结构

| 数据结构 | 谁创建 | 主要字段与用途 |
|---|---|---|
| `SessionContext` | Host | `sessionId`；绑定业务 conversation |
| `ObservationPlan` | UI Protocol Adapter | session/message/surface owner、脱敏 UI 快照、可观察组件和 action boundary |
| `ComponentEvent` | Renderer 集成层 | owner、事件类型和不可反推原值的短期 token |
| `ActivationOptions` | Host | `focused?`；控制初始 timer lease |
| `DeactivationReason` | Host/Runtime | surface 删除、替换、会话结束、Runtime 释放或 Host 请求 |

`ComponentEvent` 必填 `messageId`、`surfaceInstanceId`、`componentId` 和 `eventType`。`valueToken`、`optionToken` 只用于同一组件内比较状态，不得放原始输入或真实选项值。

## 3. UI Protocol Adapter 模块

### 3.1 模块概述

UI Protocol Adapter 隔离 A2UI 版本或客户自定义 UI 协议的差异。它重放最终实际生效的 message，识别可观察组件，生成脱敏 surface 定义和稳定 owner；在 UI 更新路径中，它还把未信任建议转换成协议可执行的安全更新。

Adapter 不生成业务 A2UI，也不解析草稿。它必须接收完成校验、修复和协议降级后真正交付 Renderer 的 message。

### 3.2 在流程中的位置与采用方式

- **上游**：A2UI Parser/Store；
- **下游**：IIAP Runtime 和可选 Suggestion Executor；
- **默认实现**：当前已验证 `A2UIV08Adapter`；
- **客户实现**：自定义协议或尚未正式支持的 A2UI 版本；
- **当前限制**：v0.1 只公开 v0.8 Adapter；v0.9.1 源码 prototype 不进入 npm 制品，不构成兼容承诺。

```ts
import { A2UIV08Adapter } from '@openjiuwen/iiap/a2ui-v08';
```

### 3.3 API 快速查找

| API | 能力 | 谁调用 | 谁实现 |
|---|---|---|---|
| `buildObservationPlans(input)` | 把一段最终 UI message 流转换为每 surface 一个 plan | A2UI Store 集成层 | 默认 Adapter 或客户 Custom Adapter |
| `validateSuggestion(value, context)` | 把未信任更新建议校验并转换为协议更新 | `onAccept` Host 代码 | 默认 Adapter 或客户 Custom Adapter |
| `new A2UIV08Adapter(policy?)` | 创建 v0.8 Adapter并注入可选更新授权 | Host 初始化代码 | SDK |
| `capabilityForV08Component(type)` | 查询 v0.8 标准组件的 IIAP 语义能力 | 自定义 Renderer 映射/测试 | SDK |
| `SuggestionPolicy.resolveAllowedValues()` | 为特定 binding 明确允许值 | Adapter | 客户，且仅 UI 更新需要 |

### 3.4 `buildObservationPlans(input)`

**接口能力**：从最终 UI message 流中还原有效 surface，排除删除、不可达和不可观察组件，为每个可观察 surface 创建一个 `ObservationPlan`。

**调用关系**：由 A2UI Parser/Store 集成层在 message 最终生效后调用；默认 Adapter 或客户 Custom Adapter 实现。

**签名**：`buildObservationPlans(input: ProtocolInput<TMessage>): ObservationPlan[]`

**最小示例**：

```ts
const plans = adapter.buildObservationPlans({
  sessionId,
  messageId,
  namespace: messageId,
  messages: finalA2UIV08Messages,
});
```

**输入**：

| 入参字段 | 来源 | 作用与约束 |
|---|---|---|
| `sessionId` | 会话管理器 | 必须与目标 `IIAPSession` 相同 |
| `messageId` | Assistant message 实例 | 归属当前 UI 内容，不是随机 render ID |
| `namespace` | Host 稳定消息命名空间 | 与协议 surface ID 组合成 `surfaceInstanceId`；不得每次重渲染变化 |
| `messages` | Parser/Store 接收的最终协议流 | 必须包含用于还原 surface 生命周期的完整相关消息 |

**返回与副作用**：返回 `ObservationPlan[]`，每个 plan 恰好属于一个 surface；不修改 Host Store，也不激活 Runtime。

**失败行为**：无可观察组件或 surface 已删除时返回空数组。Custom Adapter 不得通过猜测填充缺失 owner；必须生成稳定 owner、只保留真实可达组件、剔除 data model/action context 值并准确标记裁剪状态。

### 3.5 `validateSuggestion(value, context)`

**接口能力**：在用户接受之后，再次校验未信任的模型建议是否只操作当前 surface 和 Host 明确授权的 binding/value，并转换成实际协议更新。

**调用关系**：由 `onAccept` Host callback 在 UI 更新分支调用；默认 Adapter 或客户 Custom Adapter 实现。

**签名**：`validateSuggestion(value: unknown, context: SuggestionContext): SafeSuggestion<TUpdate> | null`

**输入**：

| 参数 | 来源 | 说明 |
|---|---|---|
| `value` | decision 的 `updateSuggestion` | 仍视为不可信输入 |
| `context.surfaceInstanceId` | 原 packet | 防止建议作用于已替换 surface |
| `context.allowedTargets` | 原 packet | Host 在生成 plan 时明确授权的上限 |
| `context.accepted` | Presenter 终态 | 只有 true 才允许返回安全建议 |

**返回与副作用**：返回协议类型的 `SafeSuggestion<TUpdate>` 或 null；只校验和转换，不写入 Store。

**失败行为**：任何 owner、path、类型、allowedValues 或接受状态不符合时返回 null，不抛错、不做宽松修复。

```ts
const safe = adapter.validateSuggestion(decision.updateSuggestion, context);
if (safe) await applyProtocolUpdate(safe);
```

### 3.6 `A2UIV08Adapter`

**接口能力**：创建能够解析 A2UI v0.8 生命周期和组件定义的默认 Adapter，并可选接入安全更新授权策略。

**调用关系**：由 Host 初始化代码创建；SDK 实现，之后由 A2UI Store 调用其两个 Adapter 方法。

**签名**：`new A2UIV08Adapter(suggestionPolicy?: SuggestionPolicy)`

**输入**：可选 `SuggestionPolicy`；只提供文字帮助时不传。

**返回与副作用**：返回无共享全局状态的 Adapter 实例；构造时不解析 message、不产生网络请求。

**失败行为**：构造本身不抛协议错误；未知组件在构建 plan 时被忽略，不自动猜测能力。

```ts
const adapter = new A2UIV08Adapter(suggestionPolicy);
```

不需要 UI 更新时省略 `suggestionPolicy`。Adapter 支持 TextField、CheckBox、Slider、DateTimeInput、MultipleChoice、Button、Tabs、Modal、Video、AudioPlayer、List 等已映射组件；未知组件不会自动变为可观察输入。

### 3.7 `capabilityForV08Component(type)`

**接口能力**：查询一个 v0.8 标准组件类型是否可观察，以及它对应的 `InteractionCapability`。Renderer 自定义语义事件映射或 Adapter conformance 测试可以使用；正常 `buildObservationPlans()` 已在内部调用同一映射。

**调用关系**：由自定义 Renderer 映射或 conformance 测试按需调用；SDK 实现。普通 Adapter 接入无需调用。

**签名**：`capabilityForV08Component(type: string): InteractionCapability | undefined`

**输入**：A2UI v0.8 组件类型名称，例如 `TextField`。

**返回与副作用**：返回 capability 或 undefined；无副作用。

**失败行为**：未知类型返回 undefined，不抛错，调用方不得凭名称猜测语义。

```ts
const capability = capabilityForV08Component('Slider'); // scalar_adjust
```

### 3.8 未发布的 v0.9.1 prototype

源码树可保留 v0.9.1 研究性 prototype 和内部测试，但 `@openjiuwen/iiap`
不导出 `./a2ui-v091`，npm tarball 也不携带该实现。客户不得 deep import
源码；正式接入 v0.9.1 时应实现经过自己协议测试的 Custom Adapter，
或等待后续版本提供完成官方 conformance 的公共入口。

### 3.9 Adapter 相关数据结构

| 数据结构 | 作用 |
|---|---|
| `ProtocolInput<TMessage>` | Host 到 Adapter 的最终 message 输入 |
| `ObservationPlan` | Adapter 到 Runtime 的单 surface 观察定义 |
| `ComponentObservation` | 组件类型、语义能力、角色、binding 和可选授权值 |
| `SurfaceContext` | 排除敏感数据后的有效 UI 定义快照 |
| `SuggestionContext` | 更新二次校验所需的 surface、白名单和接受状态 |
| `SafeSuggestion<TUpdate>` | 已通过 Adapter 校验、尚未执行的协议更新集合 |

`ObservationPlan` 的关键字段：

| 字段 | 含义与约束 |
|---|---|
| `sessionId` / `messageId` | plan 所属的业务会话和 Assistant message |
| `protocolVersion` | Adapter 实际解析的 UI 协议版本 |
| `surface.originalSurfaceId` | 协议 message 中的 surface ID，可能在不同消息轮次重复 |
| `surface.surfaceInstanceId` | `namespace + originalSurfaceId` 形成的消息实例内 owner，用于区分历史 surface |
| `surfaceContext` | 有效、脱敏、带裁剪标记的 UI 定义 |
| `components` | 可观察组件及其 capability、role、binding 和可选 allowedValues |
| `explicitActionBoundaries` | 点击后应按真实业务 action 暂停当前观察的组件 |

`surfaceInstanceId` 的稳定性来自 Host 提供的消息 namespace，而不是随机 DOM ID 或每次 render 的时间戳。用户观察到的同一消息 surface 重渲染时应保持 instance ID；历史中不同消息即使复用了协议 surface ID，也必须得到不同 instance ID。

## 4. Decision Transport 模块

### 4.1 模块概述

Decision Transport 是 UI 侧 Runtime 与服务端 IIAP API 之间的通信端口。它统一三类调用：发送意图上下文获得是否帮助的决策；用户接受后请求文字帮助；可选上传终态反馈。

Transport 不负责模型推理，也不决定是否展示 UI。它负责网络、超时、基础结构校验和请求/响应关联。

### 4.2 在流程中的位置与采用方式

- **上游**：Runtime 或 `onAccept` Host callback；
- **下游**：客户现有 HTTP/RPC/WebSocket API；
- **默认实现**：`HTTPDecisionTransport`；
- **客户实现**：复用 WebSocket、SSE 回传、RPC 或其他企业通信框架时实现 `DecisionTransport`。

```ts
import type { DecisionTransport } from '@openjiuwen/iiap';
import { HTTPDecisionTransport } from '@openjiuwen/iiap/http';
```

### 4.3 API 快速查找

| API | 能力 | 谁调用 | 谁实现 |
|---|---|---|---|
| `decide(packet)` | 获取与 packet 关联的帮助决策 | Runtime | 默认 HTTP 或客户 Transport |
| `assist(request)` | 用户接受后获取纯文字帮助 | `onAccept` Host callback | 默认 HTTP 或客户 Transport |
| `sendFeedback(feedback)` | 可选上传终态反馈 | Runtime | Transport；默认关闭 |
| `new HTTPDecisionTransport(options)` | 使用固定 HTTP endpoint 组合 | Host 初始化代码 | SDK |

### 4.4 `decide(packet)`

**接口能力**：发送隐私安全的 `IntentContextPacket`，返回与原 packet/surface 严格关联的 `IIAPDecisionEnvelope`。

**调用关系**：标准路径由 Runtime 自动调用；接口由默认 HTTP Transport 或客户 Transport 实现。

**签名**：`decide(packet: IntentContextPacket): Promise<IIAPDecisionEnvelope>`

**输入**：Runtime 生成的隐私安全 packet；Transport 不得重新填充原始表单值。

**返回与副作用**：异步返回匹配 packetId/surfaceInstanceId 的 envelope；通常产生一次网络请求，但不直接展示 UI。

**失败行为**：网络失败、超时或 envelope 非法时 Promise reject；Runtime 通过 `onError` 失败关闭。

```ts
const envelope = await transport.decide(packet);
```

### 4.5 `assist(request)`

**接口能力**：在用户接受 `text_assistance` 后，请求只作用于本轮的纯文字帮助。

**调用关系**：由 `onAccept` Host callback 调用；接口由默认 HTTP Transport 或客户 Transport 实现。

**签名**：`assist(request: AssistanceRequest): Promise<AssistanceResponse>`

**输入**：Host 根据已接受 decision 构造的关联 request；它不是普通用户消息，不显示在聊天界面或 user history。

**返回与副作用**：返回匹配 requestId 的纯文字 response；Host 决定如何展示为 assistant assistance。

**失败行为**：requestId 不匹配、结构非法、A2UI message/tag 或网络失败时 reject；失败内容不得展示。

```ts
const response = await transport.assist(request);
showAssistantAssistance(response.message);
```

### 4.6 `sendFeedback(feedback)`

**接口能力**：把已展示 offer 的 interaction/outcome 发送到远端，用于经合规评审后的分析或策略改进。

**调用关系**：由 Runtime 在远端反馈显式开启时调用；Transport 可选实现。

**签名**：`sendFeedback?(feedback: IIAPFeedback): Promise<void>`

**输入**：已通过 Runtime owner/终态校验的 feedback。

**返回与副作用**：返回 void Promise；可能产生一次上传。本地退避在上传前已经生效。

**失败行为**：默认 HTTP 配置 `feedbackUpload:false` 时安全返回且不发请求；上传失败由 Runtime 错误路径处理，不回滚本地终态。

```ts
await transport.sendFeedback?.(feedback);
```

### 4.7 `HTTPDecisionTransport`

**接口能力**：用标准 HTTP POST 实现 DecisionTransport，统一 endpoint、超时、header、响应结构和 assistance 文本校验。

**调用关系**：由 Host 初始化代码创建并注入 Runtime；SDK 实现。

**签名**：`new HTTPDecisionTransport(options: HTTPTransportOptions)`

**输入**：

```ts
const transport = new HTTPDecisionTransport({
  baseUrl: '/api/iiap',
  headers: { authorization: `Bearer ${token}` },
  timeoutMs: 10_000,
  feedbackUpload: false,
});
```

| 配置 | 必填 | 默认 | 作用 |
|---|---:|---|---|
| `baseUrl` | 是 | 无 | 自动拼接 `/decision`、`/assistance`、`/feedback` |
| `headers` | 否 | content-type | 注入鉴权/trace；不得含原始表单值 |
| `timeoutMs` | 否 | 10000 | 每个请求的中止时间 |
| `fetch` | 否 | `globalThis.fetch` | Node/测试可注入自定义实现 |
| `feedbackUpload` | 否 | false | 是否真正调用 feedback endpoint |

**返回与副作用**：返回 Transport 实例；构造不发请求。之后分别 POST `/decision`、`/assistance` 和可选 `/feedback`。

**失败行为**：没有全局 fetch 且未注入 `options.fetch` 时构造抛错；超时抛 `IIAPError('MODEL_TIMEOUT')`，网络、非 2xx 或非 JSON 抛 `IIAPError('TRANSPORT_ERROR')`，非法 decision envelope 抛 `IIAPError('INVALID_DECISION')`；assistance 内容安全失败仍抛 `ASSISTANCE_UNSAFE_OUTPUT`。

**最小示例**：见本节上方构造代码。

### 4.8 Transport 相关数据结构

| 数据结构 | 方向 | 核心语义 |
|---|---|---|
| `IntentContextPacket` | Runtime → Service | 脱敏 surface、当前事件、模式、最多 3 个历史报告和允许操作 |
| `IIAPDecisionEnvelope` | Service → Runtime | decision payload + 服务端拥有的 packet/decision/surface 关联 |
| `AssistanceRequest` | Host → Service | 已接受 decision 的关联 ID、帮助主题和语言 |
| `AssistanceResponse` | Service → Host | 匹配 requestId 的纯文字 message |
| `IIAPFeedback` | Runtime → 可选服务 | offer interaction 和 accepted 后的执行 outcome |

**`IntentContextPacket` 的主要部分**：

| 部分 | 作用 | 客户应如何理解 |
|---|---|---|
| Owner/时间字段 | packet、session、message、surface ID 和观察窗口 | 用于关联和 stale 防护，不是模型推断字段 |
| `surfaceContext` | 当前有效 A2UI 定义的脱敏快照 | 告诉模型用户在哪种 surface/component 中交互 |
| `observations.events` | 本轮按 sequence 排列的脱敏事实 | 模型判断的首要证据，不能被 pattern 替代 |
| `patterns` | 通过 event ID 回指事实的中性模式 | 提供 ABAB/重复失败等索引，不是固定业务意图 |
| `reportHistory` | 当前报告之外此前最多 3 个报告 | 让模型理解同一 surface 的近期行为变化 |
| `allowedOperations.updateTargets` | Host 明确授权的更新上限 | 只表示“最多允许什么”，不代表应该更新 |

**`IIAPDecisionEnvelope` 与 payload**：

| 字段 | 含义 |
|---|---|
| `decisionId` | 服务端为本次模型结果生成的唯一 ID |
| `packetId` / `surfaceInstanceId` | 必须复用原请求 owner，Runtime 据此拒绝错属和过期结果 |
| `payload.decision` | `offer_help`、`no_intervention` 或 `defer` |
| `payload.offerType` | `text_assistance`、`update_suggestion` 或 `none` |
| `payload.helpTopic` | text_assistance 时必填：compare_options、explain_rules、fix_block、save_progress；必须与 offer 文案一致 |
| `payload.message` / `uiStyle` | Presenter 使用的简短 offer 文案与样式提示；`uiStyle` 由 Host 依据 `offerType` 推导（`inline_card` | `none`），模型输出中的该字段一律忽略 |
| `payload.updateSuggestion` | 可选、仍未执行的 data-model 建议；必须等待用户接受和客户端复验 |

**Assistance 数据**：`AssistanceRequest` 携带 request/packet/decision/surface ID、topic 和 language；它通过关联 ID 指向已接受 decision，不复制整个 packet。`AssistanceResponse` 只允许 type、版本、匹配的 requestId 和纯文字 message。

**Feedback 数据**：`interaction` 是 accepted、dismissed、rejected、ignored 或 timed_out；只有 accepted 必须同时携带 `outcome: succeeded | failed`，其他 interaction 禁止携带 outcome。

完整字段、枚举和 JSON 示例见[Wire 协议与数据契约](../sdk-design/02-wire-protocol-and-data-contracts.md#1-wire-类型与关联规则)。

## 5. Help Presenter 模块

### 5.1 模块概述

Help Presenter 把 Runtime 的 `offer_help` 决策呈现成非阻塞 UI，并把用户的接受、拒绝、关闭或超时结果返回 Runtime。它只负责“是否接受帮助”的轻量交互，不生成帮助内容，也不执行 data-model 更新。

### 5.2 在流程中的位置与采用方式

- **上游**：Runtime；
- **下游**：应用界面和 `onAccept` callback；
- **默认实现**：React 的 `ReactHelpPresenter` + `IIAPHelpHost`；
- **客户实现**：非 React、需要自定义设计系统或原生端 UI 时实现 `HelpPresenter`。

```ts
import type { HelpPresenter } from '@openjiuwen/iiap';
import { IIAPHelpHost, ReactHelpPresenter } from '@openjiuwen/iiap/react';
```

### 5.3 API 快速查找

| API | 能力 | 谁调用 | 谁实现 |
|---|---|---|---|
| `present(decision, context)` | 展示 offer 并等待终态 | Runtime | 默认 Presenter 或客户实现 |
| `dismiss(surfaceInstanceId)` | surface 生命周期结束时撤销 offer | Runtime | 可选 Presenter 方法 |
| `new ReactHelpPresenter(options?)` | 创建实例隔离的 React Presenter | Host 初始化代码 | SDK |
| `respond(decisionId, response)` | 将 UI 点击提交给 Presenter | 自定义 React view | SDK |
| `getSnapshot()` / `subscribe()` | 读取和订阅当前 offer | React view | SDK |
| `dispose()` | 清理 offer、timer 和 listener | UI 卸载代码 | SDK |
| `<IIAPHelpHost presenter={...}/>` | 使用 SDK 默认最小帮助 UI | React 应用 | SDK |

### 5.4 `present(decision, context)`

**接口能力**：展示一个 offer，并在用户操作、超时或替换后完成 `Promise<FeedbackInteraction>`。

**调用关系**：由 Runtime 自动调用；默认 Presenter 或客户 Presenter 实现。

**签名**：`present(decision: IIAPDecision, context: {packet; decisionId}): Promise<FeedbackInteraction>`

**输入**：

| 参数 | 作用 |
|---|---|
| `decision` | 展示文案、offerType、uiStyle（Host 推导）和帮助主题 |
| `context.packet` | 确定 offer 所属 surface 和后续允许操作 |
| `context.decisionId` | 关联 UI 响应，拒绝旧 offer 的迟到点击 |

**返回与副作用**：返回最终 interaction；展示 UI 并等待用户/TTL。客户实现必须保证 Promise 最终完成。

**失败行为**：新 offer 替换旧 offer 时，旧 offer 应完成为 ignored；Presenter 异常由 Runtime `onError` 处理，不能阻塞业务。

```ts
const interaction = await presenter.present(decision, { packet, decisionId });
```

### 5.5 `dismiss(surfaceInstanceId)`

**接口能力**：surface 被删除、替换或会话退出时，撤销属于该 surface 的 offer。生命周期撤销属于 `ignored`，不能伪装成用户主动 `dismissed`。

**调用关系**：由 Runtime 在 surface 生命周期结束时调用；Presenter 可选实现。

**签名**：`dismiss?(surfaceInstanceId: string): void`

**输入**：要撤销 offer 的 surface instance ID。

**返回与副作用**：返回 void；匹配 offer 完成为 ignored 并从 UI 消失。

**失败行为**：无匹配 offer 时无操作，不抛错。

```ts
presenter.dismiss?.(surfaceInstanceId);
```

### 5.6 `new ReactHelpPresenter(options?)`

**接口能力**：创建实例隔离的 React Presenter，内置 offer 替换、TTL 和订阅状态。

**调用关系**：由 React Host 初始化代码调用；SDK 实现，并注入 Runtime 和 `IIAPHelpHost`。

**签名**：`new ReactHelpPresenter(options?: {ttlMs?: number})`

**输入**：可选 TTL，范围 5 秒到 10 分钟，默认 60 秒。

**返回与副作用**：返回 Presenter 实例；构造时不显示 UI、不启动 timer。

**失败行为**：TTL 非有限数或超出范围时抛 `RangeError`。

```tsx
const presenter = new ReactHelpPresenter({ ttlMs: 60_000 });
root.render(<IIAPHelpHost presenter={presenter} />);
```

### 5.7 `presenter.respond(decisionId, response)`

**接口能力**：把自定义 React view 中的接受、关闭或拒绝操作提交给当前 offer。

**调用关系**：由按钮/交互事件处理器调用；SDK 实现。

**签名**：`respond(decisionId: string, response: ReactHelpResponse): boolean`

**输入**：当前 decision ID，以及 accepted、dismissed 或 rejected。

**返回与副作用**：true 表示当前 offer 被结算、UI 发布 null 且 `present()` Promise 完成；false 表示未命中。

**失败行为**：旧 decision ID、重复响应或没有 offer 时返回 false，不抛错。

```ts
presenter.respond(offer.decisionId, 'accepted');
```

### 5.8 `presenter.getSnapshot()`

**接口能力**：同步读取当前 React offer，供外部状态库或首次渲染使用。

**调用关系**：由 React view/Host 调用；SDK 实现。

**签名**：`getSnapshot(): ReactHelpOffer | null`

**输入**：无。

**返回与副作用**：返回只读快照或 null；无副作用。

**失败行为**：没有当前 offer 时返回 null，不抛错。

```ts
const offer = presenter.getSnapshot();
```

### 5.9 `presenter.subscribe(listener)`

**接口能力**：订阅 offer 的创建、替换和清除，用于把 Presenter 状态接入 UI 框架。

**调用关系**：由 view mount 调用、unmount 时取消；SDK 实现。

**签名**：`subscribe(listener: (offer: ReactHelpOffer | null) => void): () => void`

**输入**：接收当前快照的 listener；注册时立即调用一次。

**返回与副作用**：返回 unsubscribe；注册期间每次状态变化通知 listener。

**失败行为**：SDK 不吞掉 listener 自身异常；Host listener 应保持轻量且不抛错。

```ts
const unsubscribe = presenter.subscribe(setOffer);
```

### 5.10 `presenter.dispose()`

**接口能力**：清理 React Presenter 的当前 offer、TTL 和 listeners。

**调用关系**：由 UI unmount/插件关闭代码调用；SDK 实现。

**签名**：`dispose(): void`

**输入**：无。

**返回与副作用**：返回 void；当前 offer 完成为 ignored，之后不再通知已清除 listeners。

**失败行为**：无当前 offer 时安全清理；重复调用无新增结果。

```ts
presenter.dispose();
```

### 5.11 `<IIAPHelpHost presenter={...}/>`

**接口能力**：提供可直接挂载的最小 React 帮助 UI，展示 offer 文案和接受/暂不需要按钮。

**调用关系**：由 React 应用渲染；SDK 实现，内部调用 `getSnapshot/subscribe/respond`。

**签名**：`IIAPHelpHost({presenter}): ReactElement | null`

**输入**：与 Runtime 共享的同一个 `ReactHelpPresenter` 实例。

**返回与副作用**：有 offer 时返回 `<aside>`，否则返回 null；按钮响应会结算 offer。

**失败行为**：传入不同 Presenter 实例会导致 UI 收不到 Runtime offer，SDK 不会自动合并实例。

```tsx
root.render(<IIAPHelpHost presenter={presenter} />);
```

Runtime 和 UI 必须持有同一个 Presenter 实例。点击接受后 Promise 应立即完成，Host 可以马上移除 offer，再异步请求 assistance。

### 5.12 Presenter 相关数据结构

| 数据结构 | 作用 |
|---|---|
| `FeedbackInteraction` | 用户/生命周期终态：accepted、dismissed、rejected、ignored、timed_out |
| `ReactHelpOffer` | decisionId、decision 和原 packet 的只读 UI 快照 |
| `ReactHelpResponse` | React UI 可主动提交的 accepted、dismissed、rejected |

## 6. Safe Suggestion 模块

### 6.1 模块概述

Safe Suggestion 是可选的 UI 更新帮助链路。它把“模型建议”与“允许实际写入客户 Store 的操作”隔离开：Host 先声明可更新 binding 和 allowedValues，服务端只能在该上限内建议，客户端在用户接受后再次校验，最后才调用客户提供的写入 callback。

只提供文字帮助时完全不需要本模块。没有显式 Policy 等于禁止 UI 更新。

```ts
import {
  ValidatedSuggestionExecutor,
  validateDataModelSuggestion,
  type SuggestionPolicy,
} from '@openjiuwen/iiap';
```

### 6.2 API 快速查找

| API | 能力 | 谁调用 | 谁实现 |
|---|---|---|---|
| `SuggestionPolicy.resolveAllowedValues(context)` | 声明某 binding 允许建议的值 | UI Protocol Adapter | 客户 |
| `validateDataModelSuggestion(value, context)` | 只校验、不执行未信任建议 | Host/Executor | SDK |
| `new ValidatedSuggestionExecutor(applyBatch)` | 绑定客户 Store 的原子批量写入 callback | Host 初始化代码 | SDK + 客户 callback |
| `executor.execute(suggestion, context)` | 再校验并提交完整安全批次 | `onAccept` callback | SDK |
| `adapter.validateSuggestion(...)` | 转换成具体 UI 协议更新 | `onAccept` callback | Adapter |

### 6.3 `SuggestionPolicy.resolveAllowedValues(context)`

**接口能力**：由 Host 决定哪个 surface/component/binding 可以被建议，以及允许哪些精确标量值。Adapter 调用它生成 packet 的 `allowedOperations.updateTargets`。

**调用关系**：由 UI Protocol Adapter 在构建 plan 时调用；客户实现。

**签名**：`resolveAllowedValues(context: SuggestionPolicyContext): UpdateValue[] | undefined`

**输入**：surface、component、bindingPath、selectionMode、maxAllowedSelections 和协议声明值等静态上下文，不包含当前用户填写值。

**返回与副作用**：返回精确允许值列表；无副作用。undefined/空数组等于没有权限。

**失败行为**：客户 Policy 抛错会使 plan 构建失败；实现应对未知字段返回 undefined，不做推断。

```ts
const policy: SuggestionPolicy = {
  resolveAllowedValues(context) {
    return context.bindingPath === '/mealType'
      ? ['vegetarian', 'meat']
      : undefined;
  },
};
```

`maxAllowedSelections` 是建议值的**形状约束**，不是授权前置条件：授权闸门由本方法返回的 `allowedValues` 决定（返回 undefined 或空数组即无权限）。声明了正整数上限时（`1` 单选、`≥2` 多选），建议值受该上限约束；未声明上限的多选字段仍可被授权，此时建议值是「白名单内的去重子集」，不比较集合大小。无论是否声明，值都必须精确属于 `allowedValues`。不能从当前值、时间、placeholder 或视觉样式推导授权或上限。

### 6.4 `validateDataModelSuggestion(value, context)`

**接口能力**：在不执行更新的情况下，校验未信任对象是否满足 accepted、surface、path、类型和 allowedValues 约束。

**调用关系**：由自定义 Host/Executor 按需调用；SDK 实现。默认 Executor 内部已调用。

**签名**：`validateDataModelSuggestion(value: unknown, context: SuggestionContext): SafeSuggestion<SafeDataModelUpdate> | null`

**输入**：模型建议和当前 packet 派生的安全上下文。

**返回与副作用**：返回安全建议或 null；无 Store 副作用。

**失败行为**：任何一条 update 不安全时整体返回 null，不抛错、不保留部分更新。

```ts
const safe = validateDataModelSuggestion(candidate, context);
```

### 6.5 `new ValidatedSuggestionExecutor(applyBatch)`

**接口能力**：创建“整批复验 + 一次提交”组件，并将完整更新数组交给 Host Store callback。

**调用关系**：由 Host 初始化代码创建；SDK 实现整批校验，客户实现原子 `applyBatch` callback。

**签名**：`new ValidatedSuggestionExecutor(applyBatch: (updates: SafeDataModelUpdate[]) => void | Promise<void>)`

**输入**：把完整更新数组作为一个事务或等价原子状态变更写入 Host Store 的 callback。

**返回与副作用**：返回 Executor；构造时不执行写入。

**失败行为**：构造本身无专用异常；applyBatch 中的错误在 execute 时向上传播。

```ts
const executor = new ValidatedSuggestionExecutor(async (updates) => {
  await hostStore.transaction(() => {
    for (const update of updates) hostStore.update(update.surfaceId, update.path, update.value);
  });
});
```

构造参数 `applyBatch` 由客户实现，因为只有 Host 知道真实 Store、事务、业务校验和刷新方式。SDK 在调用它之前检查 accepted、surface instance、重复 path、批次数量、单/多选类型、maxAllowedSelections 和 allowedValues。Host 不得把 callback 内部重新拆成可能部分提交的独立事务。

### 6.6 `executor.execute(suggestion, context)`

**接口能力**：执行用户已经接受且仍满足当前安全上下文的建议。成功时 Promise resolve；不安全建议抛 `IIAPError('UNSAFE_SUGGESTION')`，过期 surface 抛 `STALE_SURFACE`。

**调用关系**：由 `onAccept` UI 更新分支调用；SDK 实现。

**签名**：`execute(suggestion: SafeSuggestion<SafeDataModelUpdate>, context: SuggestionContext): Promise<void>`

**输入**：未执行建议，以及来自原 packet/Presenter 的 surface、allowedTargets 和 accepted 状态。

**返回与副作用**：整批通过校验后只调用一次客户 applyBatch callback；批量提交成功后 resolve。

**失败行为**：显式 `surfaceInstanceId` 与当前 context 不一致时优先抛 `STALE_SURFACE`；其他任一不安全更新使整批抛 `UNSAFE_SUGGESTION`，applyBatch 错误向上传播。两种校验错误都发生在 applyBatch 之前。失败必须反馈为 `accepted + failed`，不能执行安全子集，也不能未经再次确认自动重试。

```ts
await executor.execute(suggestion, context);
```

### 6.7 Safe Suggestion 数据结构

| 数据结构 | 核心字段 | 作用 |
|---|---|---|
| `SuggestionPolicyContext` | surface、component、binding、selectionMode、maxAllowedSelections、declaredValues | Host 做授权判断所需的静态上下文 |
| `AllowedUpdateTarget` | originalSurfaceId、bindingPath、allowedValues、可选选择模式与上限 | packet 中的更新权限上限 |
| `SuggestionContext` | surfaceInstanceId、allowedTargets、accepted | 客户端执行时的实时安全上下文 |
| `SafeSuggestion<TUpdate>` | `kind=data_model_update`、updates | 已校验但尚未执行的建议集合 |
| `SafeDataModelUpdate` | surfaceId、可选 instanceId、path、scalar/null 或完整多选数组 value | 默认 Executor 接受的一项字段更新 |

## 7. Decision/Assistance Service 模块

### 7.1 模块概述

Python Service 是模型侧的受控编排入口。它负责验证输入、构造只作用于当前 IIAP 请求的 Prompt、调用 ModelAdapter、过滤模型输出，并由服务端代码生成关联 envelope。

`DecisionService` 回答“现在是否应该提供帮助”；`AssistanceService` 只在用户接受文字帮助后生成具体的纯文字内容。它们不是独立 Agent 人设，也不会把 IIAP Prompt 注入普通业务轮次。

```python
from iiap import AssistanceService, DecisionService
```

### 7.2 API 快速查找

| API | 能力 | 谁调用 | 谁实现 |
|---|---|---|---|
| `DecisionService(model, ...)` | 创建决策编排服务 | 服务端启动/依赖注入 | SDK |
| `DecisionService.decide(packet)` | 把 packet 转换为安全 decision envelope | `/decision` handler | SDK |
| `validate_intent_context_packet(packet)` | 使用 wheel 内同版 Schema 验证 packet，失败抛 `INVALID_PACKET` | 自定义 API 边界/测试 | SDK |
| `AssistanceService(model)` | 创建文字帮助编排服务 | 服务端启动/依赖注入 | SDK |
| `AssistanceService.assist(request)` | 把已接受请求转换成安全纯文字 response | `/assistance` handler | SDK |

### 7.3 `DecisionService`

**接口能力**：创建决策编排服务，固定模型入口、运行 profile 和 decision ID 生成策略。

**调用关系**：由服务端启动或依赖注入代码创建；SDK 实现，客户提供 ModelAdapter。

**签名**：`DecisionService(model, *, profile="production", decision_id_factory=None)`

**输入**：

```python
service = DecisionService(
    model_adapter,
    profile="production",
    decision_id_factory=None,
)
envelope = await service.decide(packet)
```

| 构造参数 | 必填 | 默认 | 作用 |
|---|---:|---|---|
| `model` | 是 | 无 | 客户的 `ModelAdapter` 或 `AgentRouter` |
| `profile` | 否 | `production` | `test` 仅用于测试台强触发，不得用于生产 |
| `decision_id_factory` | 否 | UUID 工厂 | 测试可注入固定 ID；生产必须唯一 |

**返回与副作用**：返回 Service 实例；构造时不调用模型。

**失败行为**：构造无专用参数校验异常；无效 model 会在调用 `decide()` 时暴露。生产不得使用 test profile。

**最小示例**：见本节上方代码。

### 7.4 `DecisionService.decide(packet)`

**接口能力**：先使用随 wheel 发布的 Packet Schema 校验完整形状，再校验隐私和事件事实，构造决策 Prompt，调用 `generate_decision()`，最后用服务端拥有的 ID 创建 `IIAPDecisionEnvelope`。

**调用关系**：由 `/decision` HTTP/RPC handler 调用；SDK 实现，内部调用 ModelAdapter。

**签名**：`async decide(packet: dict[str, Any]) -> dict[str, Any]`

**输入**：handler 从 `iiap.intent_context_packet` envelope 中取出的 packet 字典。

**返回与副作用**：返回可直接发送给 Runtime 的 envelope 字典；有效事件会产生一次模型调用并生成新的 decisionId。

**失败行为**：Schema 失败在模型调用前抛 `ValueError("INVALID_PACKET")`，错误文本不包含 packet 内容；隐私失败或没有事件时不调用模型，安全返回关联的 no_intervention；模型 timeout/异常由客户 API 框架转换错误。

```python
envelope = await decision_service.decide(body["packet"])
```

### 7.5 `AssistanceService`

**接口能力**：创建文字帮助编排服务并绑定 ModelAdapter。

**调用关系**：由服务端启动或依赖注入代码创建；SDK 实现，客户提供 ModelAdapter。

**签名**：`AssistanceService(model: ModelAdapter)`

**输入**：客户 ModelAdapter 或满足同一协议的 AgentRouter。

**返回与副作用**：返回 Service 实例；构造时不调用模型。

**失败行为**：构造无专用异常；缺失对应模型方法会在 `assist()` 时失败。

```python
service = AssistanceService(model_adapter)
```

### 7.6 `AssistanceService.assist(request)`

**接口能力**：验证用户已接受的 AssistanceRequest，构造本轮文字帮助 Prompt，调用 `generate_assistance()`，并拒绝结构错误、A2UI 或不安全文本。

**调用关系**：由 `/assistance` HTTP/RPC handler 调用；SDK 实现，内部调用 ModelAdapter。

**签名**：`async assist(request: dict[str, Any]) -> dict[str, Any]`

**输入**：用户接受后由 Host 构造的 AssistanceRequest；检查 type、版本、四个关联 ID、topic、language、隐私和 4096-byte 上限。

**返回与副作用**：返回严格 AssistanceResponse，包含匹配 requestId 的纯文字 message；有效请求产生一次模型调用。

**失败行为**：非法请求抛 `INVALID_ASSISTANCE_REQUEST`/`PRIVACY_REJECTED`，非法响应抛 `INVALID_ASSISTANCE_RESPONSE`，A2UI/不安全文本抛 `ASSISTANCE_UNSAFE_OUTPUT`。当前不自动重试；Host 不展示或持久化失败的模型内容，但必须发送明确错误终态，禁止用空响应静默完成。

Jiuwen 的聊天 Host 不把通用 Agent 模型当作 wire service：模型提示词只要求返回帮助正文，Host 再完成关联和安全校验。这样避免模型偶发增加字段、嵌套 `payload` 或复现错误 ID 时导致有效正文整体丢失。历史版本生成的严格 `AssistanceResponse` envelope 仍可兼容解包；正文和 envelope 都无效时，流式接口发送带 `IIAPAssistanceValidationError` 的 `chat.error`。

```python
response = await assistance_service.assist(request)
```

### 7.7 Service 使用流程

```python
model = CustomerModelAdapter(...)
decision_service = DecisionService(model)
assistance_service = AssistanceService(model)

async def decision_endpoint(body: dict) -> dict:
    return await decision_service.decide(body["packet"])

async def assistance_endpoint(body: dict) -> dict:
    return await assistance_service.assist(body)
```

API handler 仍负责鉴权、请求体上限、超时、trace 和 HTTP 错误映射；SDK Service 负责 IIAP 语义与安全校验。

## 8. Model Integration 模块

### 8.1 模块概述

Model Integration 是 IIAP Service 与客户模型基础设施之间的扩展边界。Service 知道 IIAP 协议、Prompt 和输出规则，但不知道客户使用哪个模型 SDK、业务 Agent、上下文存储或模型网关；客户通过实现 `ModelAdapter` 补齐这一段。

通常客户不会主动调用 `ModelAdapter` 方法。调用方是 `DecisionService` 和 `AssistanceService`。Adapter 可以把两类请求交给同一个业务 Agent，也可以路由到独立模型；wire contract 不限定帮助来源。

```python
from iiap import AgentRouter, ModelAdapter, ModelRequest
```

### 8.2 API 快速查找

| API | 能力 | 谁调用 | 谁实现 |
|---|---|---|---|
| `generate_decision(request)` | 调用模型判断是否提供帮助 | `DecisionService` | 客户 `ModelAdapter` |
| `generate_assistance(request)` | 用户接受后调用模型生成纯文字帮助 | `AssistanceService` | 客户 `ModelAdapter` |
| `AgentRouter(...)` | 在独立模型和业务 Agent callback 间选择 | Service 通过 ModelAdapter 协议调用 | SDK |
| `AgentRouter.generate_*()` | 执行当前配置的路由 | Service | SDK |

### 8.3 `ModelAdapter`

**接口能力**：定义 Service 调用客户模型基础设施所需的最小双方法端口，使 IIAP 不依赖具体模型 SDK 或 Agent 框架。

**调用关系**：由 DecisionService/AssistanceService 消费；客户实现，或直接使用满足该协议的 AgentRouter。

**签名**：

```python
class ModelAdapter(Protocol):
    async def generate_decision(self, request: ModelRequest) -> object: ...
    async def generate_assistance(self, request: ModelRequest) -> object: ...
```

**输入**：接口本身没有构造参数；实现类的两个方法都接收 SDK 创建的 ModelRequest。

**返回与副作用**：客户实现实例被注入 Service；实际模型副作用发生在两个 generate 方法中。

**失败行为**：缺失任一方法会在对应 Service 调用时失败；SDK 不通过反射补齐或猜测模型能力。

**最小示例**：见[客户实现示例](#87-客户实现示例)。

### 8.4 `ModelAdapter.generate_decision(request)`

**接口能力**：把 IIAP 的脱敏行为上下文交给客户模型系统，产生 `offer_help`、`no_intervention` 或 `defer` 业务 payload。

**调用关系**：由 `DecisionService.decide()` 在输入检查和请求级 Prompt 构造后调用；客户实现。

**签名**：`async generate_decision(request: ModelRequest) -> object`

**输入**：operation 为 decision 的 ModelRequest；prompt 和 payload 均由 SDK Service 构造。

**客户责任**：

- 调用业务 Agent、独立模型或企业模型网关；
- 将 `request.prompt` 作为本轮 IIAP 指令，不写入全局人设；
- 将 `request.payload` 作为本次 packet 事实；
- 返回 decision payload，不生成 packetId、decisionId 或 surfaceInstanceId；
- 允许 Service 对结果进行降级和最终校验。

**返回与副作用**：返回模型业务 payload 或可解析 JSON；通常产生一次客户模型/Agent 调用，返回值随后由 Service 校验和包装。

**失败行为**：模型 timeout、取消和调用异常向 Service/API handler 传播；不得伪造 no-intervention envelope 隐藏基础设施错误。

```python
payload = await adapter.generate_decision(request)
```

### 8.5 `ModelAdapter.generate_assistance(request)`

**接口能力**：在用户已经接受文字帮助后生成具体内容，而不是再次判断是否帮助。

**调用关系**：由 `AssistanceService.assist()` 在关联、隐私和 topic 检查后调用；客户实现。

**签名**：`async generate_assistance(request: ModelRequest) -> object`

**输入**：operation 为 assistance 的 ModelRequest，payload 是 AssistanceRequest。

**客户责任**：返回字段严格匹配的 AssistanceResponse 对象/JSON，message 只能是纯文字；不得生成 A2UI、surface、工具调用展示或新的 user message。

**返回与副作用**：返回候选 AssistanceResponse；通常产生一次模型/Agent 调用，之后由 Service 做结构和文本安全校验。

**失败行为**：模型异常向 Service/API handler 传播；A2UI 或不安全内容会被 Service 拒绝且不展示。

```python
candidate = await adapter.generate_assistance(request)
```

### 8.6 `ModelRequest` 数据结构

| 字段 | 类型 | 谁填写 | 业务含义 |
|---|---|---|---|
| `prompt` | `str` | SDK Service | 当前 IIAP operation 的完整请求级指令，仅用于本轮 |
| `payload` | `dict` | SDK Service | decision 时是 packet；assistance 时是 AssistanceRequest |
| `operation` | `"decision" \| "assistance"` | SDK Service | 让客户路由器区分两类模型任务 |

`ModelRequest` 是不可变 dataclass。客户可以在 ModelAdapter 内补充自己的会话上下文，但不得修改 wire owner 或把敏感原值回填到 packet。

### 8.7 客户实现示例

```python
class CustomerModelAdapter:
    def __init__(self, model_gateway):
        self._gateway = model_gateway

    async def generate_decision(self, request: ModelRequest) -> object:
        return await self._gateway.generate_json(
            system_prompt=request.prompt,
            input=request.payload,
        )

    async def generate_assistance(self, request: ModelRequest) -> object:
        result = await self._gateway.generate_json(
            system_prompt=request.prompt,
            input=request.payload,
        )
        return result
```

生产实现还应在客户模型网关设置超时、取消、审计和模型凭证；这些属于 Host 基础设施，不由 IIAP SDK 管理。

### 8.8 `AgentRouter`

**接口能力**：提供一个满足 `ModelAdapter` 的默认路由器，在独立 ModelAdapter 与可选业务 Agent callbacks 之间选择。

**调用关系**：由服务端依赖注入代码创建并传给 Service；SDK 实现路由，客户提供 independent adapter 和可选 callbacks。

**签名**：`AgentRouter(independent, *, business_decision=None, business_assistance=None, route="independent")`

**输入**：

```python
router = AgentRouter(
    independent=model_adapter,
    business_decision=call_business_agent_for_decision,
    business_assistance=call_business_agent_for_assistance,
    route="business_agent",
)
```

| 参数 | 必填 | 默认 | 作用 |
|---|---:|---|---|
| `independent` | 是 | 无 | 默认及业务 callback 缺失时的回退 ModelAdapter |
| `business_decision` | 否 | 无 | 业务 Agent 的决策 callback |
| `business_assistance` | 否 | 无 | 业务 Agent 的文字帮助 callback |
| `route` | 否 | `independent` | `independent` 或 `business_agent` |

**返回与副作用**：返回同时满足 ModelAdapter 的 Router；构造时不调用任何模型。

**失败行为**：route 为 business_agent 但对应 callback 缺失时回退 independent，而不是失败；independent 自身异常向上传播。

**最小示例**：见本节上方代码。

### 8.9 `AgentRouter.generate_decision(request)`

**接口能力**：按 route 把 decision 请求发送给业务 callback 或 independent ModelAdapter。

**调用关系**：由 DecisionService 调用；SDK 实现。

**签名**：`async generate_decision(request: ModelRequest) -> object`

**输入**：DecisionService 构造的 ModelRequest。

**返回与副作用**：原样返回所选下游的业务 payload；调用一个下游，不做最终校验。

**失败行为**：下游异常向 Service 传播；业务 callback 缺失时使用 independent。

```python
payload = await router.generate_decision(request)
```

### 8.10 `AgentRouter.generate_assistance(request)`

**接口能力**：按 route 把 assistance 请求发送给业务 callback 或 independent ModelAdapter。

**调用关系**：由 AssistanceService 调用；SDK 实现。

**签名**：`async generate_assistance(request: ModelRequest) -> object`

**输入**：AssistanceService 构造的 ModelRequest。

**返回与副作用**：原样返回所选下游候选 response；最终纯文本校验仍由 Service 执行。

**失败行为**：下游异常向 Service 传播；业务 callback 缺失时使用 independent。

```python
response = await router.generate_assistance(request)
```

选择路由不改变 Prompt 作用域、输入输出校验或 wire contract。

## 9. Contract、Validator 与测试工具

### 9.1 模块概述

本模块提供跨语言 JSON Schema、未信任边界 validator、decision envelope helper、错误类型和确定性测试时钟。标准 Runtime/Service 已经调用必要校验，普通接入不需要逐个调用；只有自定义 Transport、服务框架、CI conformance 或单元测试才使用。

### 9.2 API 快速查找

| API/制品 | 能力 | 典型使用者 |
|---|---|---|
| `contracts/schemas/*.json` | 校验跨进程 packet、decision、assistance、feedback、error | API 网关、CI、其他语言实现 |
| `parseDecision()` | 解析模型 decision payload并安全降级 | 自定义服务封装 |
| `createDecisionEnvelope()` | 用服务端拥有的 owner 包装 payload | 自定义服务封装 |
| `parseDecisionEnvelope()` | 校验 envelope 和可选 packet owner | 自定义 Transport |
| `validatePrivacy()` | 检查禁止字段和 JSON UTF-8 大小 | 自定义输入边界/测试 |
| `validateAssistanceText()` | 拒绝空、超长和 A2UI 输出 | 自定义 assistance 通道 |
| `validateDataModelSuggestion()` | 只校验安全更新 | 自定义 Executor |
| `contains_forbidden_key()` | 定位 Python 对象中的禁止字段 | 自定义服务诊断/测试 |
| `IIAPError` | 传递稳定 SDK 错误码 | Host 错误处理 |
| `IIAP_VERSION` | 构造 v0.1 wire object | 自定义 envelope/测试 |
| `createManualClock()` | 手动推进 quiet/idle、TTL 和退避 | 单元测试 |

### 9.3 `parseDecision(value, packet?)`

**接口能力**：把未信任 JSON/对象解析成安全 decision payload，并过滤非法枚举和未授权更新。

**调用关系**：由自定义 TypeScript 服务封装调用；SDK 实现。标准 Python DecisionService 无需调用。

**签名**：`parseDecision(value: string | unknown, packet?: IntentContextPacket): IIAPDecision`

**输入**：模型输出，以及可选原 packet；提供 packet 时才能按 updateTargets 校验更新建议。

**返回与副作用**：返回安全 payload；无副作用。

**失败行为**：JSON/结构/枚举非法时安全降级为 no_intervention，不抛出模型内容。

```ts
const payload = parseDecision(modelOutput, packet);
```

### 9.4 `createDecisionEnvelope()` / `create_decision_envelope()`

**接口能力**：使用服务端拥有的 packet、surface 和 decision ID 包装模型业务 payload，避免信任模型生成关联字段。

**调用关系**：由自定义 TS/Python 服务封装调用；SDK 实现。标准 DecisionService 内部已调用 Python 版本。

**签名**：TS `createDecisionEnvelope(value, packet, decisionId)`；Python `create_decision_envelope(value, packet=..., decision_id=...)`。

**输入**：未信任模型值、原 packet 和服务端唯一 decision ID。

**返回与副作用**：返回 `IIAPDecisionEnvelope`；无网络或状态副作用。

**失败行为**：非法 payload 会先安全降级；packet 缺失关键 owner 属于调用方错误，不能让模型补齐。

```ts
const envelope = createDecisionEnvelope(modelOutput, packet, decisionId);
```

### 9.5 `parseDecisionEnvelope(value, packet?)`

**接口能力**：校验未信任 decision envelope 的结构、版本和可选 owner 关联。

**调用关系**：由自定义 TypeScript Transport 在网络边界调用；SDK 实现。HTTP Transport 内部已调用。

**签名**：`parseDecisionEnvelope(value: unknown, packet?: IntentContextPacket): IIAPDecisionEnvelope | null`

**输入**：网络响应，以及可选预期 packet。

**返回与副作用**：返回有效 envelope 或 null；无副作用。

**失败行为**：结构或 owner 不匹配时返回 null，不抛错；Transport 应据此 reject 请求。

```ts
const envelope = parseDecisionEnvelope(responseBody, packet);
```

### 9.6 `validatePrivacy()` / `validate_privacy()`

**接口能力**：递归检查禁止字段，并验证对象 JSON UTF-8 大小是否在上限内。

**调用关系**：由自定义 TS/Python 输入边界或测试调用；SDK 实现。Runtime/Service 已内置调用。

**签名**：TS `validatePrivacy(value, maxBytes?)`；Python `validate_privacy(value, max_bytes=32768)`。

**输入**：任意待传输对象和可选字节上限。

**返回与副作用**：返回 boolean；不修改原对象。

**失败行为**：禁止字段、循环引用、`BigInt` 等 JSON 不可序列化输入、属性访问异常或超限时返回 false，不抛出序列化异常，也不返回敏感字段路径。TS `packetByteSize()` 对无法得到 JSON 字符串的输入返回 `Infinity`，避免其以零字节对象通过。

```ts
if (!validatePrivacy(packet)) throw new Error('PRIVACY_REJECTED');
```

### 9.7 `validateAssistanceText()` / `validate_assistance_text()`

**接口能力**：校验模型帮助是否为可展示的纯文字，并拒绝空、超长、A2UI tag/message。

**调用关系**：由自定义 assistance Transport/Service 或测试调用；SDK 实现。默认两端路径已调用。

**签名**：TS `validateAssistanceText(message)`；Python `validate_assistance_text(message)`。

**输入**：未信任模型 message。

**返回与副作用**：TS 返回 `{valid, reason?}`，Python 返回 `(valid, reason)`；无副作用。

**失败行为**：不安全内容返回 invalid，不自动修复、不截断展示。

```ts
const validation = validateAssistanceText(modelMessage);
```

### 9.8 `contains_forbidden_key(value)`

**接口能力**：单独判断 Python 对象是否递归包含 IIAP 禁止字段，供诊断和定向测试使用。

**调用关系**：由 Python 自定义服务测试/诊断调用；SDK 实现。生产校验优先使用 `validate_privacy()`。

**签名**：`contains_forbidden_key(value: Any) -> bool`

**输入**：任意 Python 对象。

**返回与副作用**：返回 boolean；不修改对象，也不输出字段值。

**失败行为**：没有禁止字段返回 false；本函数不检查总字节大小。

```python
assert not contains_forbidden_key(packet)
```

### 9.9 `validate_decision(value, packet=...)`

**接口能力**：在 Python 中校验模型 decision payload，并依据 packet 的允许更新目标过滤建议。

**调用关系**：由自定义 Python 决策服务调用；SDK 实现。标准 DecisionService 通过 envelope helper 间接使用。

**签名**：`validate_decision(value, *, packet=None) -> dict`

**输入**：模型候选值和可选 packet。

**返回与副作用**：返回安全 payload；无副作用。

**失败行为**：非法结果降级为 no_intervention，不信任模型关联字段。

```python
payload = validate_decision(model_output, packet=packet)
```

### 9.10 `default_no_intervention()`

**接口能力**：生成统一的 Python 安全默认 decision payload。

**调用关系**：由自定义服务在无需/无法调用模型时调用；SDK 实现。

**签名**：`default_no_intervention() -> dict`

**输入**：无。

**返回与副作用**：返回新的 no_intervention payload；无副作用。

**失败行为**：无专用失败。

```python
payload = default_no_intervention()
```

### 9.11 `validate_v08_decision(value, packet?)`

**接口能力**：兼容 JiuwenSwarm 历史 v0.8 decision 形态并转换成安全 payload。

**调用关系**：仅由迁移兼容层调用；SDK 实现。新服务使用通用 validator。

**签名**：`validate_v08_decision(value, packet=None) -> dict`

**输入**：历史 decision 和可选 packet。

**返回与副作用**：返回兼容后的安全 payload；无副作用。

**失败行为**：非法历史结果降级，不应扩展为新公共协议能力。

```python
payload = validate_v08_decision(legacy_value, packet)
```

### 9.12 `new IIAPError(code, message, retryable?)`

**接口能力**：创建 Host 可识别的稳定 SDK 错误，区分错误码、展示安全的 message 和是否可重试。

**调用关系**：由 SDK 或客户自定义 Transport/Executor 抛出；SDK 实现错误类型。

**签名**：`new IIAPError(code: IIAPErrorCode, message: string, retryable = false)`

**输入**：稳定 code、安全 message 和可选 retryable；message 不得包含敏感 payload。

**返回与副作用**：返回 Error 实例；构造无副作用。

**失败行为**：未知 code 在 TypeScript 编译期拒绝；安全错误通常不可重试，网络重试不能绕过 stale/owner 检查。

```ts
throw new IIAPError('TRANSPORT_ERROR', 'decision endpoint failed', true);
```

### 9.13 `IIAP_VERSION` 常量

只读值 `'0.1'`，用于自定义 wire 对象。它表示 IIAP wire version，不是 A2UI protocol version，不应被 Host 改写。

### 9.14 `createManualClock(start?)`

**接口能力**：创建可手动推进的确定性时钟，验证 quiet/idle、Presenter TTL 和反馈退避。

**调用关系**：仅由单元测试创建并注入 Runtime；SDK 实现。

**签名**：`createManualClock(start?: number)`

**输入**：可选起始毫秒时间。

**返回与副作用**：返回支持 `now()`、timer 和 `advanceBy(ms)` 的测试时钟；推进时执行到期 callback。

**失败行为**：不应进入生产 Runtime；测试必须显式推进时间才能触发 timer。

```ts
const clock = createManualClock(0);
clock.advanceBy(2_000);
```

`validateDataModelSuggestion()` 已在 [Safe Suggestion 模块](#64-validatedatamodelsuggestionvalue-context)按同一标准说明。

## 10. 完整接入骨架

下面的代码显示模块如何组合；完整可运行版本见 [`examples/typescript/complete-v08.ts`](../../../examples/typescript/complete-v08.ts) 和 [`examples/python/complete_service.py`](../../../examples/python/complete_service.py)。

```ts
const adapter = new A2UIV08Adapter();
const transport = new HTTPDecisionTransport({ baseUrl: '/api/iiap' });
const presenter = new ReactHelpPresenter();

const runtime = createIIAPRuntime({
  transport,
  presenter,
  onAccept: async (decision, context) => {
    if (decision.offerType === 'text_assistance' && decision.helpTopic) {
      const response = await transport.assist(
        buildAssistanceRequest(decision, context),
      );
      showAssistantAssistance(response.message);
    }
  },
});

const session = runtime.createSession({ sessionId });
for (const plan of adapter.buildObservationPlans(protocolInput)) {
  handles.set(
    plan.surface.surfaceInstanceId,
    session.activate(plan, { focused: plan === latestPlan }),
  );
}

// Renderer 原有事件入口
handles.get(event.surfaceInstanceId)?.observe(event);
```

其中只有 `buildAssistanceRequest`、`showAssistantAssistance`、`protocolInput`、Renderer 事件映射和 endpoint 属于 Host Integration Glue。事件聚合、触发判断、packet 隐私、decision 关联和反馈退避都留在 SDK 内部。

## 11. 接入顺序与完成检查

1. Adapter：用固定 UI fixture 验证 `message → ObservationPlan`；
2. Runtime：建立 session/surface/handle 生命周期；
3. Renderer：输入脱敏事件，只验证 packet；
4. Transport + Service + ModelAdapter：闭环 decision；
5. Presenter + Assistance：闭环用户接受、拒绝和纯文字帮助；
6. 如有需要，最后开放 SuggestionPolicy 和 Executor；
7. 验证历史 surface、焦点转移、stale decision、超时和服务失败。

接入完成时，客户应能回答：每个 API 属于哪个模块；由谁调用、由谁实现；输入从哪里产生；返回值交给谁；失败是否能安全回到原业务流程。
