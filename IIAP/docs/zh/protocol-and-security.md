# 协议与安全

最后更新：2026-09-23

## 1. 数据流

1. UI Protocol Adapter 从 final A2UI message 构造单 surface ObservationPlan。
2. Renderer 只把语义事件和临时 token 交给 Runtime。
3. Runtime 在 quiet/idle 条件和本地阈值满足时生成 IntentContextPacket。
4. 后端根据 packet 返回 offer_help、no_intervention 或 defer。
5. Presenter 非阻塞展示帮助；用户接受后 Host 执行 assistance 或安全 data-model update。
6. interaction/outcome 通过 packet、decision 和 surface owner 形成反馈。

Schema 位于 contracts/schemas/，是 wire 结构的权威来源。

## 2. Packet

IntentContextPacket 包含：

- packet/session/message/surface owner；
- 脱敏的有效 UI definition；
- 当前窗口内按序排列的语义事件；
- 基于事件的确定性 pattern；
- 此前最多三个已报告窗口；
- Host 明确授权的 update target。

packet 不包含原始输入、原始文本、当前 data model 值、稳定 hash 或 action context 值。
valueToken 和 optionToken 只在同一组件和 surface instance 内表达相等关系。

## 3. 本地 pattern

Runtime 对 text、boolean、scalar、temporal、option、overlay、navigation、media 和 viewport
能力执行计数或状态反转检测。Pattern 是帮助决策的中性索引，不是用户意图或业务结论；
basedOnEvents 必须回指 packet 中仍然存在的事件。

普通短暂操作不形成 packet。事件最多保留 128 个，裁剪时报告
observations.completeness.complete=false 和真实丢弃数。

## 4. Owner 与生命周期

- 事件必须匹配 messageId + surfaceInstanceId + componentId。
- decision 必须匹配 pending packet 和当前 focused surface。
- decisionId、packetId 和 feedback 均拒绝 duplicate。
- focus 转移、surface 替换/删除、session 结束和显式 suspend 会使旧请求或卡片失效。
- 一个 session 可保留多个 surface handle，但只有 focused surface 持有自动 timer。

## 5. Suggestion 安全边界

更新必须同时满足：

- Host 的 SuggestionPolicy 为绑定路径提供有限 allowedValues；
- 模型建议只包含 1～8 个不同字段；
- 每个 surface/path/value 精确匹配原 packet target；
- 单选使用标量，多选使用去重完整集合；
- 声明 maxAllowedSelections 时不超过上限；
- 用户已明确接受；
- 执行时 surface instance 仍然有效；
- 整批 all-or-nothing。

v0.1 只允许可逆 data-model update，不允许 submit、action、创建/删除 surface、支付或下单。

## 6. Assistance 与模型输出

Decision 模型输出会被结构化恢复并执行 fail-closed 校验。非法输出变成
no_intervention，诊断只记录不含内容的分类，不记录模型原文。

text_assistance 的 wire response 必须是关联正确的 iiap.assistance.response，message
必须通过纯文本安全校验。SDK Service 不生成 A2UI、UI action 或业务操作。

## 7. Production 与 Test

两种 profile 共享同一 wire、安全校验、更新授权和执行路径。Test 只能降低本地阈值或给模型
指定确定性验收场景，不能读取原值、自动接受、放宽 allowlist 或把负向 decision 改成帮助。

## 8. 已知限制

- 内置 Adapter 只正式支持 A2UI v0.8；v0.9.1 需要自定义 Adapter。
- v0.8 sanitizer 排除 data model、action context、非标准组件属性和未知组件属性，packet 报告
  unknownCustomPropertiesExcluded: true。服务端还会递归拒绝大小写和分隔符变体的敏感字段名；
  DecisionService 不会把标记为未排除未知属性的 definition 发给模型。Host 仍不得把秘密写入
  合法的可见静态文案字段。
- SDK 自动化不能替代模型内容质量、键盘/读屏、移动端和业务文案验收。
