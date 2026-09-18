# 帮助类型与强触发配方

最后更新：2026-09-18

适用范围：IIAP v0.1.0 · A2UI v0.8 · JiuwenSwarm 集成环境

状态：本文的触发阈值、组件能力与判定来源均取自当前源码，已在 `sess_1a0ad7dea95_8fa4cd` 的真实日志上复核。

## 1. 本文回答什么

“为什么我只触发得出选项对比？”以及“想测某一类帮助，页面和提示词该怎么配？”本文给出一张**从页面交互到帮助类型的完整映射**，以及每一类的强触发配方。

本文只描述"怎么让某一类帮助稳定出现"。协议字段与算法定义见[设计 02](../sdk-design/02-wire-protocol-and-data-contracts.md)和[设计 03](../sdk-design/03-runtime-observation-assistance-and-security.md)。

## 2. 先分清三个层次

“帮助类型”在日常讨论里被混用，实际是三个独立的层次，**它们不是一一对应的**：

| 层次 | 取值 | 由谁决定 |
|---|---|---|
| `decision` | `offer_help` / `no_intervention` / `defer` | 模型判断 |
| `offerType` | `text_assistance` / `update_suggestion` / `none` | 模型判断 |
| `helpTopic` | `compare_options` / `explain_rules` / `fix_block` / `save_progress` | 模型声明，缺失时由 Host 按 pattern 保守兜底 |

关键点：**`no_intervention` 和 `defer` 是终态，不展示任何 UI，因此没有 `helpTopic`。** 它们不是"另一种帮助类型"，而是"这次不帮"。

`uiStyle` 不是第四个层次——它是展示提示，由 Host 依据 `offerType` 推导（`inline_card` | `none`），模型不推理该字段。

## 3. 完整触发链路

```
用户操作组件
  → ① 语义事件（interactionBridge 脱敏后发出）
  → ② 按 capability 聚合成 signal（跨过阈值才产生）
  → ③ signal 映射成 pattern
  → ④ packet 达到阈值判定，发往模型
  → ⑤ 模型判断 decision / offerType / helpTopic
  → ⑥ Host 兜底：helpTopic 缺失时按 pattern 推断
  → ⑦ 校验：offerType 与 updateSuggestion 是否合法
  → ⑧ 展示或静默
```

**这个链条解释了为什么实际可触发的类型远少于契约允许的类型**：任何一环不满足，最终都表现为"什么都没发生"，而前端无法区分是模型判断不帮、还是中途被丢弃。

## 4. 组件能力 → 事件 → pattern → 帮助类型

> 本节是面向"排查触发不了"的操作视角展开，**不定义规则**。capability → signal → pattern 与阈值的权威定义在[设计 03 §1](../sdk-design/03-runtime-observation-assistance-and-security.md#1-观察与中性模式)，选择语义在[设计 04](../sdk-design/04-adapters-compatibility-testing-and-release.md)。本节若与上述文档冲突，以上述文档为准，并按[文档维护规则](../documentation-maintenance.md)修正本节。

### 4.1 组件能力映射

A2UI v0.8 组件到 IIAP capability 的映射（`IIAP/adapters/a2ui-v0.8/src/index.ts`）：

| A2UI 组件 | capability | 可发出的事件 |
|---|---|---|
| `TextField` | `text_edit` | `change`（带 `validityState`） |
| `CheckBox` | `boolean_toggle` | `change` |
| `Slider` | `scalar_adjust` | `change` |
| `DateTimeInput` | `temporal_edit` | `change` |
| `MultipleChoice` | `option_select` | `change`（多选时带 `optionToken` + `selectionState`） |
| `Button` | `action_invoke` | `explicit_action` |
| `Tabs` | `content_navigate` | `tab_change` |
| `Modal` | `overlay_reveal` | `open` / `close` |
| `Video` / `AudioPlayer` | `media_control` | `play` / `pause` / `seek` |
| `List` | `viewport_navigate` | `scroll` |

### 4.2 capability → signal → pattern

`IIAP/implementations/typescript/packages/core/src/aggregation.ts`：

| capability | signal | 触发阈值（production） | pattern type |
|---|---|---|---|
| `option_select` 单选 | `choice_churn` | `optionChangeCount` = 3 次 change | `selection_reversal` |
| `option_select` 多选 | `choice_churn` | `optionChangeCount` = 3 次**反转** | `selection_reversal` |
| `boolean_toggle` | `toggle_churn` | `booleanToggleCount` = 3 | `state_alternation` |
| `scalar_adjust` | `scalar_churn` | `scalarChangeCount` = 4 | `state_alternation` |
| `temporal_edit` | `datetime_churn` | `temporalChangeCount` = 3 | `state_alternation` |
| `text_edit` | `validation_block` | `textValidationFailureCount` = 2 次 invalid | `validation_failure_sequence` |
| `overlay_reveal` | `rule_inspection` | `overlayOpenCount` = 2 | `repeated_open` |
| `content_navigate` | `detail_revisit` | `navigationChangeCount` = 3 | `repeated_navigation` |
| `media_control` | `media_revisit` | `mediaControlCount` = 5 | `repeated_media_control` |
| `viewport_navigate` | `navigation_revisit` | `viewportChangeCount` = 6 | `repeated_viewport_navigation` |

**单选的阈值是"切换次数"，多选的是"反转次数"**，这是最容易踩错的地方，详见 §6.1。

### 4.3 pattern → helpTopic 兜底

仅当**模型没有声明** `helpTopic` 时才生效（`iiap/validation.py::_infer_help_topic`）：

```
validation_failure_sequence  →  fix_block
selection_reversal           →  compare_options
其余（含无 pattern）          →  explain_rules
```

注意 `save_progress` **不在兜底表里**——它只能由模型显式声明。

## 5. 为什么生产环境只触发得出"选项对比"

这是**有确定的代码依据的**，不是偶发。三个原因叠加：

### 原因一：`MultipleChoice` 是最容易被判定"值得帮"的组件

单选切换 3 次 / 多选反转 3 次就形成 pattern，而 `selection_reversal` 兜底到 `compare_options`。表单类 demo 里最自然的操作就是反复点选项，所以这条路径最先被触发。

### 原因二：其他 pattern 依赖的组件在生成的 UI 里很少出现

`Tabs` / `Modal` / `Video` / `List` 在渲染层是注册的（`rendererRegistry.tsx`），但**A2UI 生成提示词明确禁止生成它们**（`prompt_instructions.py::unsupported_component_rule_en`）：

> A2UI 0.8 does not support modal, dialog, popup, alert, toast, floating overlay, or closeable window components.

所以 `repeated_open` / `repeated_navigation` / `repeated_media_control` / `repeated_viewport_navigation` 这几类在常规生成的表单里**根本没有组件能产生**。

### 原因三：`update_suggestion` 的授权面比看上去窄得多

界面修改建议要求 packet 里有非空 `allowedOperations.updateTargets`。而更新目标只对**协议静态声明了有限取值**的字段生成——这里必须区分两件事：**授权闸门**（这个字段准不准改）和**合法值来源**（改成什么值才算合法）。闸门对任何绑定到 data model 的组件都是开的，但**合法值必须能枚举**，否则执行边界只能"相信模型"，等于允许凭空编造 UI 事实。

当前只有两类组件能提供可枚举的值域（`declaredValues()`）：

| 组件 | 能否成为更新目标 | 原因 |
|---|---|---|
| `MultipleChoice` | ✅ | `props.options` 显式枚举全部合法取值 |
| `CheckBox` | ✅ | A2UI 0.8 的 CheckBox 是无参数布尔开关，值域恒为 `{true, false}` |
| `Slider` | ❌ | 协议只有 `minValue`/`maxValue`、**没有 `step`**，值域连续，无法枚举 |
| `DateTimeInput` | ❌ | 任意时刻，值域无界 |
| `TextField` | ❌ | 自由文本，值域无界 |
| `Button` | ❌ | action 组件，无 data model 值 |

注意 `Slider` 与 `CheckBox` 的区别：两者都"看起来有范围"，但 `CheckBox` 的值域是**两个离散值**（平凡可枚举、可逆），而 `Slider` 没有 `step` 意味着 0 到 5 之间可以是任意实数。**有界 ≠ 可枚举**，这是这条限制的真正边界。

`CheckBox` 在 2026-09 之前未被授权，属于实现只覆盖了 `options` 一条路径的**缺口**，而非安全原则的要求（其值域是整个协议里第二简单的）。

即使存在更新目标，模型还可能因提示词的安全约束而选择 `text_assistance`。所以**生产环境看到的多半是 `compare_options`**，这与你的观察一致。

## 6. 强触发配方

以下配方针对**生产模式**（test 模式还可使用测试控制台直接指定场景，见 §7）。

### 6.1 选项对比（`compare_options`）— 最容易

**单选组件：**

```
在同一组单选题上，依次点击 3 个不同选项，最后回到起始选项。
例：日料 → 韩餐 → 西餐 → 日料
```

在 `option_select` 且 `selectionMode=single` 时，阈值是 `optionChangeCount`（production = 3）次 `change`。

**多选组件（关键差异）：**

```
在同一组多选题上，把同一个选项"选中 → 取消 → 选中"，
或让两个选项交替选中/取消，凑够 3 次反转。
例：日料 选中 → 日料 取消 → 日料 选中 → 日料 取消
```

多选走的是 `multipleSelectionReversals()`：只有当**同一个 optionToken 的 selectionState 发生变化**时才计数。因此**依次选中 A、B、C 不算反转**（那是正常填写），必须是"选中又取消"这种来回。

多选组件若未显式声明 `maxAllowedSelections`，`variant: chips` 或 `checkbox` 会按多选处理（保守判定）。

### 6.2 规则说明（`explain_rules`）— 兜底型

```
在一个没有明确 pattern 的组件上产生少量交互，
然后等待触发判定。
```

`explain_rules` 是 `_infer_help_topic` 的**默认分支**，所以任何能触发上报但 pattern 不在兜底表里的窗口，都会落到它。它也是模型在"看到异常但说不清类型"时最常自己声明的 topic。

**注意**：这条路径最不稳定，因为它依赖模型主动选择帮助，而模型在证据弱时倾向 `no_intervention`。

### 6.3 阻断修复（`fix_block`）— 生产环境较难

```
在一个 TextField 里，重复输入不满足校验的内容，
凑够 2 次 invalid 事件。
```

`text_edit` 的判定依据是 `eventType === 'validation_error'` 或 `validityState === 'invalid'`，阈值 `textValidationFailureCount` = 2。

**生产环境有两个实际障碍：**

1. `interactionBridge` 只在 `change` 事件上附带 `validityState`，而 `validation_error` 事件本身**只在测试控制台的合成事件里产生**（`globalRuntime.ts::emitSyntheticThresholdEvents`）。所以生产只能靠 `change` 携带 `invalid` 累计。
2. `validityState` 的来源是 `textFieldValidity()`：优先用组件的 `validationRegexp`，否则回落到浏览器原生 `target.validity.valid` 与 `aria-invalid`。而**A2UI 生成提示词并不要求生成 `validationRegexp`**，常规生成的普通文本框因此几乎永远是 `valid`。

要让它稳定出现，需要页面上存在**带约束的输入框**：

- 组件显式声明了 `validationRegexp`（当前生成的表单里很少见）；
- 或用了原生约束类型，如 `textFieldType: 'number' | 'date'`（映射到 `<input type="number|date">`，空值或非法输入会让 `validity.valid` 为 false）。

**结论**：`fix_block` 在纯生成的 demo 表单里通常触发不了。要演示这一类，**建议在测试控制台直接指定场景**，或手工构造一个带 `validationRegexp` 的 surface。

### 6.4 保存进度（`save_progress`）— 需要模型显式声明

```
先制造一个明显的 selection_reversal 或较长的填写过程，
让模型认为"当前有值得保留的工作"。
```

`save_progress` **不在兜底表里**，只能由模型显式声明 `helpTopic`。提示词对它的限定也很窄：

> 只有确需保存或稍后继续时才用 save_progress

**这是四类里最难强触发的一类。** 当前没有确定性的页面操作能让它稳定出现。如果 demo 必须展示它，**唯一可靠的方式是测试控制台的场景指定**（§7）。

### 6.5 界面修改建议（`update_suggestion`）— 需要满足两个条件

同时满足才可能出现：

**条件一：页面里有 `MultipleChoice` 或 `CheckBox` 组件**（只有这两类能生成更新目标，见 §5 原因三）。

**条件二：制造一个明显的反转模式**，让模型有理由提出"帮你把某个字段设成 X"。

```
在带 MultipleChoice / CheckBox 的表单上，对同一选项或开关反复切换，
凑够多次反转，然后等待触发。
```

**即使两个条件都满足，模型仍可能返回 `text_assistance`。** 提示词对 `update_suggestion` 的要求很严：每个值必须严格属于 `allowedValues`、单选必须是单个标量、多选必须是完整去重集合且不超上限、`message` 必须逐项列出字段和目标值。任一不满足，整批作废并降级为文字帮助。

`CheckBox` 的目标值只能是布尔标量 `true` / `false`——字符串 `"true"`、数字 `1`、或数组都会被整批拒绝。

**如果你需要稳定演示界面修改建议，测试控制台是唯一可靠路径**（§7）。

## 7. 测试控制台：强触发的可靠方式

测试模式提供 `IIAPTestConsole`，可直接指定场景，绕过模型判断（但仍执行真实的 packet、传输与模型调用）：

| 场景 | 效果 |
|---|---|
| `compare_options` | 强制输出选项对比文字帮助 |
| `explain_rules` | 强制输出规则说明 |
| `fix_block` | 强制输出阻断修复 |
| `save_progress` | 强制输出保存进度 |
| `update_suggestion` | 强制输出界面修改建议（需 surface 有可授权目标） |
| `no_intervention` | 强制负向，静默 |
| `defer` | 强制延迟决策，静默 |
| `invalid_json` | 强制非法输出，验证 fail closed |
| `cycle` | 按上表顺序循环 |

触发模式：`threshold`（跨阈值）、`first_event`（首个事件）、`manual`（手动）。

**`update_suggestion` 在控制台下的额外要求**：目标 surface 必须存在可授权的静态更新目标。若没有，控制台会明确报错"当前 surface 没有可授权的静态更新目标"，而不会静默降级——这是有意设计，避免把"没有目标"伪装成"模型不想帮"。

## 8. 触发间隔与上限（生产模式）

连续触发会被以下 gate 拦截，**验证配方时要留足间隔**：

| 配置项 | 值 | 含义 |
|---|---|---|
| `componentQuietMs` | 2000 | 组件停止交互多久后才考虑上报 |
| `surfaceIdleMs` | 10000 | surface 整体空闲多久后才考虑上报 |
| `minReportIntervalMs` | 30000 | **两次上报之间的最小间隔** |
| `maxReportsPerComponent` | 4 | 单组件最多上报几次 |
| `maxReportsPerSurface` | 4 | 单 surface 最多上报几次 |

**最常见的"点了没用"原因**：`minReportIntervalMs` 是 30 秒。一次触发后，**30 秒内无论怎么操作都不会有第二次上报**，日志会显示 `flush_skipped reason=report_interval`。其他常见拦截原因：`no_pattern`（交互量没到阈值）、`in_flight`（上一次判定还没回来）、`feedback_backoff`（用户刚对上一张卡片做过响应）。

**另一个容易误判为 bug 的现象**：`maxReportsPerComponent` 是 4。**同一个组件上报满 4 次后，后续事件在入口就被丢弃**（`runtime.ts::emitComponentEvent` 直接返回），因此不会形成新的 signal，日志显示 `flush_skipped reason=no_pattern`——看起来像"阈值突然变高了"。换一个新组件，或刷新页面重开会话，即可继续测试。`maxReportsPerSurface` 是 4，打满后日志显示 `reason=surface_report_limit`，此时必须重开会话。

## 9. 排查：触发不了时看什么

判断"是模型不帮"还是"中途被丢弃"，查后端日志（`~/.jiuwenswarm/agent/.logs/agent_server.log`）：

| 日志 | 含义 |
|---|---|
| `[IIAP] packet received` | packet 已发出，端侧触发链正常 |
| 无后续日志 | packet 未到达决策服务，查传输 |
| `[IIAP] decision validated: decision=offer_help` | 模型给了正向帮助 |
| `[IIAP] decision validated: decision=no_intervention` | **模型确实判断无需介入** |
| `[IIAP] model decision rejected ... parse_status=... reject_reason=...` | **模型给了决策但被校验丢弃** |
| `frontend.runtime.flush_skipped reason=...` | 端侧 gate 拦下，看 `report_interval` / `no_pattern` / `in_flight` / `feedback_backoff` |

最后一行是区分"模型不帮"与"被系统丢弃"的关键。`reject_reason` 只记录字段名与取值类别，不记录模型原文。

## 10. 现有文档中的相关条目

- 协议字段与判定规则：[设计 02](../sdk-design/02-wire-protocol-and-data-contracts.md)、[设计 03](../sdk-design/03-runtime-observation-assistance-and-security.md)
- capability → signal → pattern 映射与阈值表：[设计 03 §1](../sdk-design/03-runtime-observation-assistance-and-security.md#1-观察与中性模式)
- MultipleChoice 单选/多选语义与授权边界：[设计 04](../sdk-design/04-adapters-compatibility-testing-and-release.md)
- 测试模式与场景：[Demo、测试与故障排查](05-demos-testing-and-troubleshooting.md)
