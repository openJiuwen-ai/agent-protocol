# Runtime、观察、帮助与安全

最后更新：2026-09-16

本篇定义 Runtime 的观察算法、并发与反馈状态，以及 Decision/Assistance 的提示词和安全边界。字段的精确 wire 形式见[Wire 协议与数据契约](02-wire-protocol-and-data-contracts.md)。

## 1. 观察与中性模式

端侧规则只判断“这组事实是否值得发送给模型”，不判断用户的业务意图。达到阈值后，packet 同时保留脱敏事件事实和由这些事件支持的中性模式：

| 语义能力 | 事件/计数依据 | 输出模式 | 默认阈值 |
|---|---|---|---:|
| `text_edit` | `validation_error` 或 invalid | `validation_failure_sequence` | 2 |
| `boolean_toggle` | change | `state_alternation` | 3 |
| `scalar_adjust` | change | `state_alternation` | 4 |
| `temporal_edit` | change | `state_alternation` | 3 |
| `option_select`（single） | 连续改选 | `selection_reversal` | 3 |
| `option_select`（multiple） | 同一 option 的 selected/cleared 反转 | `selection_reversal` | 3 次反转 |
| `overlay_reveal` | open | `repeated_open` | 2 |
| `content_navigate` | tab_change | `repeated_navigation` | 3 |
| `media_control` | play/pause/seek | `repeated_media_control` | 5 |
| `viewport_navigate` | 带 token 的 scroll | `repeated_viewport_navigation` | 6 |

多选正常增加不同选项不会累计反转；只有同一 `optionToken` 在 selected/cleared 之间来回变化才计数。所有 pattern 必须用 `basedOnEvents` 引用 packet 中的事件，不能输出 `comparison_need`、`flow_stall` 或 `user_confused` 等业务结论。模型即使忽略 `patterns`，仍能从 `observations.events` 解释本次报告。

## 2. 触发策略与报告历史

默认策略：

| 配置 | 默认值 | 含义 |
|---|---:|---|
| `componentQuietMs` | 2000 ms | 组件停止交互后尝试自动 flush |
| `surfaceIdleMs` | 10000 ms | focused surface 空闲后尝试自动 flush |
| `minReportIntervalMs` | 30000 ms | 同一 surface 两次报告最小间隔 |
| `maxReportsPerSurface` | 4 | 同一 surface observation lineage 的报告上限 |
| `maxReportsPerComponent` | 1 | 同一 surface 中同 ID 组件的报告上限 |
| `reportHistoryLimit` | 3 | 当前报告之外携带的历史报告数量 |
| `packetMaxBytes` | 32768 | packet 最大字节数 |

quiet/idle 到期不等于必然上报。`flush()` 还要依次通过：surface 仍 focused、无 in-flight、反馈退避结束、报告间隔与上限允许、至少存在一个达到阈值的 pattern，以及隐私和体积校验。

`observations.events` 最多 128 条、`reportHistory` 最多 3 条是 wire schema 硬上限，不是可由 Host 放宽的运行参数。`PolicyOverrides.maxEventsPerPacket` 和 `reportHistoryLimit` 可以进一步收紧，但超过硬上限时 SDK 必须钳制到 128 和 3，不能生成违反 schema 的 packet。

JiuwenSwarm 的 test profile 为了避免录制和人工填写时仅两次改选就过早出现帮助，将 `optionChangeCount` 单独设为 4：前三次有效 `change` 不上报，第四次才形成选择切换模式。production profile 保持 3，不受该测试参数影响。测试控制台的手动“立即触发”会合成超过阈值的事件，因此仍可确定性触发指定场景。为了让连续场景矩阵可执行，test profile 把所有反馈退避设为 3 秒；这仍走同一 Feedback gate，不绕过 cooldown，production 的默认退避保持不变。

`RuntimeOptions.onFlushSkipped` 可以仅向安全诊断记录输出 `reason + surfaceInstanceId + automatic`。它用于区分反馈退避、报告间隔、无 pattern、上限和隐私拒绝，不改变 `flush()` 的生产语义，也不得输出完整 packet 或原始交互值。

每个 packet 主体是“当前一次报告”，`reportHistory` 另外携带同一 `surfaceInstanceId` 此前最多三个已完成报告。因此默认语义是“当前 1 次 + 之前最多 3 次”，不是总共三次，也不是最近三个 DOM event。

## 3. Session 与多 surface 并发

一个 Runtime 可以有多个 session；一个 session 可以注册会话历史中的多个 surface，但同时只有一个 focused surface 拥有自动 timer lease：

1. 新 plan 默认取得关注权；Host 也可用 `{ focused: false }` 注册，再显式 `focus()`。
2. 后台 surface 不运行 quiet/idle timer，因此聊天历史中的旧表单不会仅因时间经过而触发 IIAP。
3. 用户直接操作后台 surface 时，完整 owner 校验通过后，关注权转移到该 surface，并为它建立新窗口。
4. 关注权转移会取消旧 surface 的 timer、丢弃未上报临时事件，并使旧 pending/presented decision stale；已完成报告历史继续保留。
5. 相同 `surfaceInstanceId` 的新 plan 只替换该 surface 的旧状态，保留已完成历史和报告计数；其他 surface 不受影响。

SDK 保留两级身份：

- `originalSurfaceId`：UI 协议提供的原始 ID；
- `surfaceInstanceId`：Host namespace 与原始 ID 的组合。

v0.1 不对随机值或时间戳进行启发式裁剪，也不增加 continuity key。namespace 或原始 ID 变化就表示新的 surface instance；若协议未来提供稳定逻辑 ID，必须通过显式契约接入，而不能猜测 ID 格式。

### 3.1 Owner 与 stale 防护

Renderer 事件必须在任何关注权、timer 或窗口变化之前，通过 `messageId + surfaceInstanceId + componentId` 精确匹配。缺失 owner、消息/surface 错配和未知组件分别以 `missing_owner`、`owner_mismatch`、`unknown_component` 拒绝，诊断不记录事件值。同名 component 可以出现在不同 surface，因为 owner 是完整三元组。

Runtime 用 `packetId` 关联产生 packet 的 handle 和关注代次。Decision 必须同时通过 envelope、packet owner、surface 关注状态和 `decisionId` 去重；异步响应在 surface 失焦、暂停、注销或替换后到达，只产生安全诊断，不能展示帮助或影响当前 surface。

每次激活返回的释放句柄只能注销自己仍然拥有的状态；旧 Renderer 延迟 cleanup 不能误删同 instance 的新 plan。

### 3.2 suspend 与 assistance turn

用户主动发送新聊天请求或触发显式业务 action 时，Host 调用 `session.suspend()`：历史 surface 仍注册，但当前 timer lease、未上报窗口和 pending/presented decision 被清除。surface 删除、会话结束或 Runtime 销毁才 deactivate。

IIAP 自己发起的 AssistanceRequest 不是用户聊天消息：

- 不在界面中渲染为 user message，也不写入 user history；
- 不调用 `suspend()`，不改变原业务 surface 的注册或关注权；
- assistance 回复即使违规包含 A2UI，也不能注册为新的观察 surface。

## 4. Decision 提示词契约

DecisionService 每次调用都构造请求级提示词，包含：

- IIAP 协议字段解释；
- 紧邻 packet 的固定短提醒；
- 普通操作不干预、状态交替、连续校验失败、安全更新、白名单外降级和不完整事实等 few-shot；
- UI 文本属于非可信数据、不得作为指令的约束。

提示词要求模型以 `observations.events` 为首要事实，以 `surfaceContext` 理解当前 UI，把 `patterns` 当作可选索引，并将 `allowedOperations` 仅视为最大执行边界。模型不能把 token 还原为真实值、把 pattern 说成确定意图，或补写因裁剪而缺失的事实。

IIAP 不定义 Agent 人设。Decision prompt 只用于 `decide`，Assistance 的纯文本约束只用于 `assist`；Host 不得把任一提示词安装为全局 prompt，也不能让“禁止 Assistance 生成 A2UI”影响普通业务轮次。

Prompt 不是安全校验的替代品。模型输出仍必须经过服务端 validator，更新建议还必须经过客户端 Adapter 和 Executor。

## 5. 帮助、Presenter 与反馈

### 5.1 Decision 结果

- `offer_help`：Presenter 展示可选帮助；业务流程继续可用。
- `no_intervention`：结束本次 in-flight，继续观察后续窗口。
- `defer`：不展示帮助，等待后续事实。

默认 React Presenter 的 Offer、Listener 和 Timer 都属于实例。每个实例最多同时展示一个 Offer；新 Offer 替换旧 Offer 时，旧 Promise 返回 `ignored`。用户主动关闭返回 `dismissed`；生命周期 `dismiss(surfaceInstanceId)` 返回 `ignored`。

TTL 默认 60 秒，允许配置范围 5 秒至 10 分钟，到期返回 `timed_out`。所有终态清理 timer；`respond(decisionId, ...)` 只接受当前 Offer，旧 UI 的延迟点击不能完成新 Offer。`uiStyle` 在 v0.1 只是 Host 展示提示，默认 Presenter 统一使用帮助卡片；该值由 Host 依据 `offerType` 推导而非由模型推理，模型输出中的 `uiStyle` 一律忽略。

### 5.2 接受帮助

接受文字帮助时，Host 调用 AssistanceService。SDK 只规定输出必须是纯文本且不能包含 A2UI、结构化 UI、UI action 或自动执行指令；业务 Agent、专用模型或 Host Router 都可以作为提供者。

TypeScript 客户端和 Python 服务端用共享 corpus 做最终校验。违规输出返回 `ASSISTANCE_UNSAFE_OUTPUT`：当前版本单次调用后直接失败关闭，不重试、不抽取局部文本、不持久化模型输出。Host 必须把失败作为明确错误终态呈现，不能以空 `chat.final` 静默完成；该 interaction 记录为 `accepted + failed`，而不是 `ignored`。

Jiuwen Host 使用通用对话模型提供帮助时，模型只生成最终文字正文（可含 Markdown），Host 负责请求关联、协议边界和最终安全校验，不要求模型复现 `AssistanceResponse` envelope。Host 仍兼容解包历史严格 envelope；结构化输出格式错误或文本不安全时发送可见 `chat.error`，不泄露原始模型输出。

接受更新建议时，Host 的 `onAccept` 把建议交给 Adapter/Executor 进行最后校验。执行完成后才记录 accepted 的 `succeeded` 或 `failed`。

### 5.3 Feedback 与本地退避

Feedback 默认仅保存在 session 内存，远端上传由 Host 显式启用。默认退避时间为：

| 结果 | 默认退避 |
|---|---:|
| accepted + succeeded | 15 秒 |
| dismissed | 120 秒 |
| ignored / timed_out | 60 秒 |
| rejected | 300 秒 |
| accepted + failed | 300 秒 |

反馈不携带用户文本、字段值、帮助正文或完整 decision。未知、关联错误、重复和 stale feedback fail closed，不能改变本地退避。

## 6. 隐私与数据最小化

禁止字段包括 `rawValue`、`rawText`、`rawEvents`、`rawEvent`、`valueHash` 和 `textHash`。token 只能是 Host 生成的、作用域为“同一 surface instance 内的同一组件”的短期类别标识，不能跨组件比较或编码原值。

Adapter 发送脱敏后的有效 A2UI definition snapshot，而不是原始 message history：

- 排除 `dataModelUpdate/updateDataModel` 和当前 data model；
- 排除 action context 的值；
- 排除已删除或不可达组件；
- 未知 custom property fail closed；
- 静态标签可以保留用于理解界面，但按非可信数据处理；
- 裁剪状态和事件丢弃数量必须分别写入 redaction/completeness。

日志只允许记录标识符、计数、pattern 类型、稳定错误码和耗时，不记录原始内容、token 映射或完整 packet。

隐私检查必须对循环引用、`BigInt` 等 JSON 不可序列化输入以及访问属性时抛错的对象 fail closed：返回拒绝结果，不得让序列化异常逃逸并中断业务 UI。不可得到 JSON 字符串的输入不允许以零字节对象通过体积检查。

## 7. 安全更新

更新权限默认拒绝。Adapter 默认产生空的 `updateTargets`；Host 必须通过 `SuggestionPolicy.resolveAllowedValues()` 为具体 surface、component、binding 返回非空 `allowedValues`。A2UI 静态选项可以作为 Host 策略输入，但 Adapter 不能自行把它们转成执行权限。

授权必须区分**闸门**与**合法值来源**：闸门决定"该字段准不准改"，对任何绑定到 data model 的组件都是可开放的；但 `allowedValues` 必须**能枚举**，否则执行边界只能信任模型，等于允许凭空编造 UI 事实。因此只有协议显式给出有限值域的组件才能成为更新目标——A2UI v0.8 中即 `MultipleChoice`（`options` 枚举）与 `CheckBox`（无参数布尔开关，值域恒为 `{true, false}`）。`Slider` 虽有 `minValue`/`maxValue` 但协议无 `step`，值域连续，**有界不等于可枚举**，因此不授权；`DateTimeInput` 与 `TextField` 值域无界，同样不授权。

每个建议至少在 DecisionService 输出端以及客户端 Adapter/Executor 执行前完成校验。校验必须类型敏感，避免 JavaScript/Python 把布尔与数字视为等价。下列情况都不能执行：

- surface、path、value 类型或值不匹配；
- 缺少非空白名单；
- 声明了选择模式与 `maxAllowedSelections` 时两者矛盾（如 single 配非 1、multiple 配小于 2），或多选建议含重复值、越界值、超过已声明上限；
- 批次包含重复 path、超过 8 个字段，或其中任一更新不安全；
- 建议调用 action/submit/payment；
- 建议修改结构、创建或删除 surface；
- 用户尚未明确接受。

Executor 在通用 suggestion 校验前先核对建议中显式携带的 `surfaceInstanceId`。它与当前 `SuggestionContext.surfaceInstanceId` 不一致时必须抛出 `STALE_SURFACE`，而不是被通用失败折叠成 `UNSAFE_SUGGESTION`；两种错误都不能调用 Host Store callback。

一次建议可以批量修改多个字段；选择型字段以显式正整数 `maxAllowedSelections` 为形状事实（`1` 使用单个值，`≥2` 使用完整选择集合），未声明上限时退化为「白名单内的去重子集」而不做集合大小比较。非选择型标量字段不需要该属性。整批在决策校验和执行边界都采用 all-or-nothing 校验，不允许静默过滤非法项后部分执行。非法 `update_suggestion` 在决策校验阶段降级为文字帮助；执行边界再次发现不一致时 fail closed，并记录 `accepted + failed`。Host Store callback 必须把收到的批次作为一个事务或等价的原子状态变更提交。

Presenter 还必须把校验后的整批变更渲染为可核对的预览，逐项显示字段与目标值；不能只显示“可以帮你修改”之类的泛化文案。模型生成的自然语言可以解释原因，但不能代替由结构化 suggestion 派生的变更清单。多选预览显示的是应用后的完整集合，不是一个模糊的增量动作。
