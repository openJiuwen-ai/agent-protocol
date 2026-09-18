# 自定义 Adapter、配置与安全

最后更新：2026-08-13

## 1. 自定义 UI 协议或组件

非标准 A2UI 组件应通过 capability map 或自定义 `UIProtocolAdapter<TMessage,TUpdate>` 接入。Adapter 只承担两项职责：把最终 UI message 转为独立的 ObservationPlan；把模型候选更新收敛为协议可执行的 SafeSuggestion。公共类型和接口用法以[SDK 模块、接口与数据结构开发参考](02-sdk-modules-capabilities-and-interfaces.md)为准，跨进程字段以[Wire 协议与数据契约](../sdk-design/02-wire-protocol-and-data-contracts.md)为准。

实现时必须遵守：

- 每个 surface 返回独立 plan，不把多个 surface 的组件合并。
- 只声明 Host 确认可观察、在有效组件树内且属于该 surface 的组件。
- 自定义组件显式映射 capability；未知组件和未知 operation 默认不可观察、不可更新。
- Renderer 事件携带 Plan 中精确的 message、surface instance 和 component owner。
- Host 不从 DOM 顺序推断 owner，不裁剪或重新生成 `surfaceInstanceId`。
- `validateSuggestion` 同时检查 operation、surface、instance、binding path、值类型、白名单和用户明确接受。

## 2. 显式授权 UI 更新

SDK 默认不允许修改任何 data model 字段。只有 Host 能枚举非敏感、安全且业务有效的值时，才配置 SuggestionPolicy：

```ts
import { A2UIV08Adapter } from '@openjiuwen/iiap/a2ui-v08';

const adapter = new A2UIV08Adapter({
  resolveAllowedValues(context) {
    if (context.originalSurfaceId === 'booking'
      && context.bindingPath === '/reservationTime') {
      return ['2026-08-05T18:00:00+08:00', '2026-08-05T19:00:00+08:00'];
    }
    return undefined;
  },
});
```

返回 `undefined`、空数组或非法 scalar 表示不授权。A2UI message 中声明的静态选项只作为 `declaredValues` 供策略判断，不自动成为白名单；模型输出也永远不能扩展白名单。被授权字段的建议值必须精确属于 allowedValues；声明正整数 `maxAllowedSelections` 时（`1` 单选、`≥2` 多选）还受该上限约束，未声明上限的多选字段退化为「白名单内的去重子集」而不做集合大小比较。test profile 可以提供确定性候选值，但不能绕过 Adapter 与 Executor 的整批校验。

## 3. 模型与 Agent 路由

ModelAdapter 可以指向独立模型，也可以复用业务 Agent。选择由客户的上下文、成本和部署方式决定，不改变 IIAP contract：

- Decision 请求需要 UI 定义、脱敏事件事实、模式和允许操作。
- Assistance 请求来自用户接受文字帮助后的内部 turn。
- IIAP prompt 只在对应请求中注入，不定义 Agent 身份，不影响普通业务轮次生成 A2UI。
- Assistance 只允许纯文本；违规输出当前版本直接忽略，不重试。

## 4. Transport、鉴权和错误隔离

默认 HTTP Transport 支持 `baseUrl`、自定义 `fetch`、headers、timeout 和 feedback 开关。生产集成还应由 Host 负责：

- 使用现有鉴权和租户隔离，不把凭据写入 packet 或日志；
- 对 timeout、非 2xx、非法 JSON、关联 ID 不匹配按失败处理；
- 不因 IIAP 失败阻断 A2UI 渲染、表单提交或普通 Agent 流程；
- 使用 request/packet/decision ID 记录诊断，但不记录原始输入值；
- 在 session、message 和 surface 维度限制重放和迟到响应。

## 5. 配置原则

Runtime 配置分为 deadline/threshold、report limit、privacy/packet size 和 feedback/backoff 四组。生产默认值由 Core 提供；宿主可以在测试和业务评审后覆盖，但不能关闭：

- 禁止字段与 packet 大小检查；
- 完整事件 owner 校验；
- suggestion 双端验证和用户明确接受；
- stale surface 防护；
- assistance 纯文本校验。

测试环境可以降低触发阈值或调用 `flush()`，但必须显式标记测试配置，不能把低阈值作为生产默认值。

## 6. 隐私检查表

- Renderer 只发送事件种类和短期 token，不发送文本、真实选项或 data model 值。
- Adapter 的 surface snapshot 排除 data model、action context values、不可达组件和未知自定义属性。
- 自定义字段加入 snapshot 前必须单独评审，并沿用 packet 大小上限。
- feedback 默认仅本地保存；上传前明确数据保留周期、用途和删除策略。
- 日志只记录关联 ID、状态和错误码，不转储 packet 或模型完整 prompt。

隐私生命周期、触发算法和失败关闭规则的权威说明见[Runtime、观察、帮助与安全](../sdk-design/03-runtime-observation-assistance-and-security.md)。
