# Wire 协议与数据契约

最后更新：2026-08-13

本篇是 IIAP v0.1 跨进程 wire protocol 与数据结构的人类可读权威说明。机器可执行的最终约束以 `IIAP/contracts/schemas/` 中的 JSON Schema 为准，跨语言样例位于 `IIAP/contracts/fixtures/`。

本文只回答“端侧、服务端和 Host 之间传输什么数据、字段如何关联”。模块功能、TypeScript/Python 公共 API、参数、返回值、默认实现和客户实现责任统一见[SDK 模块、接口与数据结构开发参考](../sdk-integration/02-sdk-modules-capabilities-and-interfaces.md)；安装方式见[分步接入](../sdk-integration/03-step-by-step-integration.md)。

## 1. Wire 类型与关联规则

| wire 类型 | 用途 | 权威关联字段 |
|---|---|---|
| `iiap.intent_context_packet` | 客户端观察结果 | envelope 内的 `packet.packetId`、`surfaceInstanceId` |
| `iiap.decision` | 主动帮助决策 | `decisionId`、`packetId`、`surfaceInstanceId` |
| `iiap.assistance.request/response` | 接受文字帮助后的请求/响应 | `requestId`，request 同时携带原 decision owner |
| `iiap.feedback` | 已展示 decision 的交互与执行终态 | packet、decision、surface、offer 四项关联 |
| `iiap.error` | 稳定错误码与安全诊断 | `requestId` 可选 |

所有 wire 对象都必须携带 `type` 与 `iiapVersion: "0.1"`。业务 payload 与传输元数据分层；模型只生成 Decision payload，不生成或复述 `packetId`、`decisionId` 等关联 ID。

### 1.1 IntentContextPacket

Packet 不包含 `signals`、`hypotheses` 或含义不明确的 `uiContext`，而由职责清晰的五部分组成：

- `surfaceContext`：由 Adapter 重放增量 UI message 后生成的脱敏有效 surface 定义；
- `observations.events`：本窗口按 `sequence` 排列的脱敏事实，是模型判断的首要证据；
- `patterns`：可选的中性确定性索引，每项必须通过 `basedOnEvents` 回指事实；
- `reportHistory`：当前窗口之外、同一 surface instance 此前最多三个已完成报告；
- `allowedOperations`：Host 明确授权的安全更新上限，不代表 SDK 推荐执行更新。

正式 envelope 示例：

```json
{
  "type": "iiap.intent_context_packet",
  "iiapVersion": "0.1",
  "packet": {
    "iiapVersion": "0.1",
    "packetId": "pkt_01",
    "sessionId": "session-1",
    "messageId": "message-1",
    "originalSurfaceId": "preferences",
    "surfaceInstanceId": "message-1:preferences",
    "protocolVersion": "0.9.1",
    "timestamp": "2026-08-05T12:00:00.000Z",
    "window": {
      "startTime": "2026-08-05T11:59:55.000Z",
      "endTime": "2026-08-05T12:00:00.000Z",
      "durationMs": 5000
    },
    "surfaceContext": {
      "protocol": "a2ui",
      "protocolVersion": "0.9.1",
      "snapshotType": "sanitized_effective_definition",
      "definition": [],
      "redaction": {
        "dataModelExcluded": true,
        "actionContextValuesExcluded": true,
        "unreachableComponentsExcluded": true,
        "unknownCustomPropertiesExcluded": true,
        "truncated": false
      }
    },
    "observations": {
      "tokenScope": "component_within_surface_instance",
      "events": [],
      "completeness": {"complete": true, "droppedEventCount": 0}
    },
    "patterns": [],
    "reportHistory": [],
    "allowedOperations": {"updateTargets": []}
  }
}
```

`ComponentEvent.messageId`、`surfaceInstanceId` 和 `componentId` 均为必填 owner。事件中的 `valueToken`、`optionToken` 只表达同一 surface instance、同一组件内的状态相等关系；多选还可用 `selectionState: "selected" | "cleared"` 表达同一选项的反转，不能携带原始选项值。

### 1.2 Decision

```json
{
  "type": "iiap.decision",
  "iiapVersion": "0.1",
  "decisionId": "dec_01",
  "packetId": "pkt_01",
  "surfaceInstanceId": "message-1:preferences",
  "payload": {
    "decision": "offer_help",
    "reason": "comparison_need",
    "offerType": "text_assistance",
    "helpTopic": "compare_options",
    "uiStyle": "inline_card",
    "message": "我可以帮你梳理这些选项。"
  }
}
```

`decision` 只能是 `offer_help | no_intervention | defer`；`offerType` 只能是 `text_assistance | update_suggestion | none`。Runtime 以 `packetId` 查找待处理请求，再校验 surface、关注代次和 `decisionId` 去重。未知、错配、已失焦、已暂停、已注销或重复的响应全部 fail closed。

`offer_help + text_assistance` 必须给出与 offer 文案一致的 `helpTopic`。兼容旧模型遗漏该字段时，Host 仅可依据已验证的交互模式做保守兜底：`selection_reversal` 映射为 `compare_options`，`validation_failure_sequence` 映射为 `fix_block`，其他文字帮助映射为 `explain_rules`；不得把普通 `model` 场景默认改成 `save_progress`。显式测试场景仍可固定指定 topic。

`uiStyle` 是 **Host 拥有的展示提示，不由模型推理**：Host 依据 `offerType` 推导 —— `text_assistance | update_suggestion` → `inline_card`，`none` → `none`。模型输出中的任何 `uiStyle` 一律忽略。理由：该字段在 v0.1 只影响"是否展示卡片"的二值开关，把它交给模型推理会新增一个纯粹由措辞决定的失败点（实测：模型输出不存在的 `inline_hint`，导致整份正确的 `offer_help` 决策被 fail closed 成 `no_intervention`，与真正的负向结果完全同形且前端静默）。展示层不得作废决策；真正的边界是 §2 的值白名单。因此 wire 上的 `uiStyle` 取值域收敛为 `inline_card | none`。

### 1.3 Assistance

```json
{
  "type": "iiap.assistance.request",
  "iiapVersion": "0.1",
  "requestId": "assist_01",
  "packetId": "pkt_01",
  "decisionId": "dec_01",
  "surfaceInstanceId": "message-1:preferences",
  "topic": "compare_options",
  "language": "zh-CN"
}
```

Response 只包含 `type`、`iiapVersion`、匹配的 `requestId` 和 `message`。Request 通过关联 ID 引用已接受的 decision，不内嵌 IntentContextPacket；服务端/Host 负责按自己的状态和路由补充业务上下文。该选择不强制服务提供者是业务 Agent 还是独立模型。

TypeScript 的 `validateAssistanceText()` 与 Python 的 `validate_assistance_text()` 使用共享 corpus 保持结论一致。它们拒绝空消息、超限内容、A2UI 标签和 A2UI message；对外失败码统一为 `ASSISTANCE_UNSAFE_OUTPUT`。

### 1.4 Feedback

```json
{
  "type": "iiap.feedback",
  "iiapVersion": "0.1",
  "packetId": "pkt_01",
  "decisionId": "dec_01",
  "surfaceInstanceId": "message-1:preferences",
  "offerType": "text_assistance",
  "interaction": "accepted",
  "outcome": "succeeded",
  "timestamp": "2026-08-05T12:00:05.000Z"
}
```

`interaction` 是 `accepted | dismissed | rejected | ignored | timed_out`。`accepted` 必须携带 `outcome: succeeded | failed`，其他 interaction 禁止携带 outcome。Runtime 只接受与已展示 decision 的 `packetId + decisionId + surfaceInstanceId + offerType` 全部匹配的第一条终态反馈。

## 2. 安全更新数据契约

`allowedOperations.updateTargets[].allowedValues` 是严格执行白名单。更新必须同时满足：

- `originalSurfaceId`、`bindingPath` 和目标组件匹配；
- 单选或普通标量目标使用 JSON scalar/null，且类型和值精确匹配 `allowedValues`；
- 多选目标的建议值是选中集合数组；数组去重、每项都精确属于 `allowedValues`，携带正整数 `maxAllowedSelections` 时还不得超过该上限；
- 一次建议包含 1～8 个互不重复的 surface/path 更新；任一更新非法时整批拒绝，不保留或执行安全子集；
- 用户已经明确接受。

白名单只能来自 Host 显式配置的 `SuggestionPolicy`；Adapter 不根据组件类型、当前时间、placeholder、默认值或静态选项自动授权。空白名单等于无更新权限。安全算法和失败行为见[Runtime、观察、帮助与安全](03-runtime-observation-assistance-and-security.md)。

## 3. 版本与大小边界

- IIAP wire 版本固定为 `0.1`，A2UI 协议版本是独立的 Adapter 兼容维度。
- packet 默认总上限为 32768 bytes；AssistanceRequest 隐私校验上限为 4096 bytes；AssistanceResponse message 最大 4096 字符。
- `surfaceContext.definition` 是脱敏后的增量 message 序列，每个元素是一条可重放 message。条数不设上限；其内的组件总数最多保留 128 个，并在快照约 16 KiB 时从尾部裁剪，裁剪必须用 `surfaceContext.redaction.truncated` 标记。
- `observations.events` 每个报告窗口最多 128 条。Runtime 保留最近事实，丢弃数量必须写入 `observations.completeness.droppedEventCount`；只要发生任何裁剪，`complete` 就必须为 `false`。裁剪后仍存在于数组中的 `eventId` 才能被 `patterns.basedOnEvents` 引用。
- `testScenario` 是仅由宿主 test profile 写入的可选字段，用于固定测试场景。production packet 禁止携带；服务端只能因它改变决策倾向，不得因此放宽隐私、安全或白名单校验，也不得改写其他协议事实。
- `additionalProperties: false` 的 Schema 对未知字段 fail closed；未知安全敏感操作不能降级为宽松执行。
