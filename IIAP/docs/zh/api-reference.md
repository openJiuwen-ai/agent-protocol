# API 参考

最后更新：2026-09-23

本文只记录 v0.1 稳定公共面。未从 package 根入口或 package.json exports 暴露的源码符号
均为实现细节。

## 1. TypeScript 根入口

### Runtime

- createIIAPRuntime(options?)：创建 Runtime。
- runtime.createSession({sessionId})：同一 ID 的新 session 会终止旧 session。
- session.activate(plan, {focused?})：激活单 surface 观察并返回 handle。
- session.focus(surfaceInstanceId)：转移自动 timer lease。
- session.suspend()：清除当前事件、pending decision 和已展示卡片。
- session.handleDecision(envelope)：校验 owner、stale 和 duplicate 后处理 decision。
- session.cancelDecision(packetId)：取消 Host 托管的外部请求。
- session.recordFeedback(feedback)：校验并记录外部反馈。
- deactivate() / dispose()：释放 surface、session 或全部 Runtime 状态。

RuntimeOptions 的 packet 发送方式互斥：

- {onPacket, onFeedback?, transport?: never}：Host 托管；
- {transport, onPacket?: never}：SDK 托管；
- 二者都省略：只生成 packet，由调用方手工获取。

两种模式共享 policy、presenter、onAccept、feedbackUpload、诊断回调和时钟。启用
feedbackUpload 时，Host 托管模式必须提供 onFeedback，SDK 托管模式的 Transport 必须实现
sendFeedback；缺少反馈发送端会在创建 Runtime 时立即报错，避免静默丢失用户反馈。

### 安全与工具

- validatePrivacy(value, maxBytes?)：拒绝禁止字段、非有限数字、循环/不可序列化值和超限数据。
- packetByteSize(value)：返回 JSON UTF-8 字节数；不可序列化时返回正无穷。
- validateAssistanceText(message)：拒绝空内容、超长内容和 A2UI 内部消息。
- validateDataModelSuggestion(value, context)：按接受状态、surface/path/value 和选择基数校验整批更新。
- ValidatedSuggestionExecutor(applyBatch)：执行前再次校验，并只调用一次原子 batch callback。
- coerceModelObject(value)：从合法 JSON、完整 JSON fence，或只有前置说明且以唯一 JSON
  对象结尾的模型文本恢复对象；拒绝对象后的非空尾部和多对象输出。
- IIAPError：携带稳定 code 和 retryable。

根入口同时导出 wire、Runtime、Adapter port、Presenter port、Suggestion port 和 Policy 类型。
完整类型签名以 npm 制品中的 dist/index.d.ts 为准。

## 2. TypeScript 子入口

| 入口 | 公共能力 |
|---|---|
| @openjiuwen/iiap/a2ui-v08 | A2UIV08Adapter、capabilityForV08Component |
| @openjiuwen/iiap/decision | parseDecision、parseDecisionEnvelope |
| @openjiuwen/iiap/http | HTTPDecisionTransport |
| @openjiuwen/iiap/react | ReactHelpPresenter、IIAPHelpHost |
| @openjiuwen/iiap/testing | createManualClock |

v0.1 不提供 a2ui-v091 子入口或源码 prototype。

## 3. Python 根入口

### Service 与模型 port

- DecisionService(model, profile='production', decision_id_factory=None)
- AssistanceService(model)
- ModelAdapter、ModelRequest、AgentRouter

DecisionService.decide(packet) 在模型调用前执行完整 packet Schema 和隐私校验，返回带
服务端 owner 的 decision envelope。production profile 拒绝携带 testScenario 的 packet。

### Prompt 与校验

- decision_prompt(packet, profile='production', language='en', source='sdk')
- validate_intent_context_packet(packet)
- validate_decision(value, packet=...)
- validate_v08_decision(value, packet=...)
- create_decision_envelope(value, packet=..., decision_id=...)
- describe_decision_parse_status(value)
- describe_decision_rejection(value)
- validate_assistance_request(request)
- validate_assistance_text(message)
- validate_privacy(value, max_bytes=32768)
- contains_forbidden_key(value)

Python 包不提供业务聊天 UI 的纯文本/Markdown prompt；这属于 Host 展示和 Agent 集成责任。

## 4. 客户实现责任

- Adapter：把已验证 UI 协议转换为单 surface ObservationPlan。
- Transport：认证、网络、超时和服务端 endpoint。
- Presenter：非阻塞展示，并返回用户的真实 interaction。
- SuggestionPolicy：只授权绑定路径和有限值域。
- applyBatch：对当前 data model 做原子更新，不触发 action。
