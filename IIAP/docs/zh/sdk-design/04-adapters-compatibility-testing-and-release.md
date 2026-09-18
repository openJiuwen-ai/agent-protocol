# Adapter、兼容、测试与发布

最后更新：2026-09-18

本篇定义 UI 协议隔离、语言与版本演进原则、验证层次、发布门和 JiuwenSwarm 迁移边界。面向客户的实际版本支持状态只在[兼容矩阵](../sdk-integration/compatibility-matrix.md)维护。

## 1. A2UI Adapter 策略

IIAP Core 只消费 `ObservationPlan` 和 `ComponentEvent`，不解析 A2UI message。每个 `UIProtocolAdapter` 负责：

1. 重放指定版本的增量 message，得到当前有效 surface；
2. 为每个 surface 生成独立 ObservationPlan；
3. 将 standard/custom catalog component 映射为 IIAP 语义能力；
4. 构造脱敏 `surfaceContext`；
5. 根据 Host `SuggestionPolicy` 生成安全更新目标；
6. 在客户端校验模型返回的更新建议。

A2UI v0.8 Adapter 是当前已经过 JiuwenSwarm 回接和 fixture 验证的实现，处理 `beginRendering`、`surfaceUpdate`、`dataModelUpdate` 和 `deleteSurface`。A2UI v0.9.1 是下一目标基线；当前只保留未发布的源码 prototype，没有 npm 子入口，且尚未支持官方扁平 `component: "TextField"` 结构、`ChoicePicker` 语义和完整 catalog conformance，因此不能用于正式生产接入。未来完成 v0.9.1 或新增协议版本时，不修改 Core Contract，也不把 A2UI 版本绑定到 IIAP 版本。

未知组件默认不观察。自定义 Renderer 应显式提供 capability、componentRole 和 bindingPath，不能依靠名称猜测。更新权限仍由 Host `SuggestionPolicy` 返回严格 `allowedValues`，Adapter 不从默认值或静态选项自动授权。

`MultipleChoice.maxAllowedSelections` 优先于视觉 `variant` 判定选择语义：值为 1 时按单选，大于 1 时按多选；未声明时，chips/checkbox 保守按多选处理。该规则只影响观察和更新能力，不读取当前 data model 推测用户值。

上述选择语义在 production 和 test profile 中保持一致。Host Policy 授权某个绑定时，`maxAllowedSelections` 只是值的形状约束，不是授权前置条件：声明正整数上限时（`1` 单选、`≥2` 多选），建议值必须受该上限约束；未声明上限的多选字段仍可被授权，但建议值退化为「白名单内的去重子集」，不做集合大小比较。无论是否声明上限，值都必须精确属于 `allowedValues`。测试模式不得改变这些协议事实。

## 2. 语言、包与版本策略

- TypeScript 实现客户端 Runtime、A2UI Adapter、HTTP Transport、React Presenter 和 Executor。
- Python 实现 DecisionService、AssistanceService、ModelAdapter/AgentRouter 和服务端验证。
- JSON Schema 与共享 fixtures 是跨语言 wire 真源；任一语言实现不得以本地类型覆盖 Schema。
- HarmonyOS 可在后续基于同一 Schema 实现 ArkTS Adapter/Runtime，v0.1 不因此改写已验证的 Python 服务。

SDK 版本由 `IIAP/VERSION` 管理，源码 tag 格式为 `iiap-vX.Y.Z`：

- 破坏 wire contract 或已发布公共 API：提升 major；
- 新增向后兼容字段或能力：提升 minor；
- 不改变兼容契约的修复：提升 patch。

TypeScript 逻辑模块都属于单一 npm 包 `@openjiuwen/iiap`，通过子入口暴露；它们不独立版本化。Python 参考包为 `openjiuwen-iiap`。两个语言包必须共享相同的 IIAP wire 版本，但可以按各自生态构建。

## 3. 测试与一致性

### 3.1 Contract

- 每类 Schema 的 canonical 正例和负例；
- TypeScript/Python round trip 与 validator 结论一致；
- unknown field、错误枚举、oversize、forbidden key 和非法安全操作；
- packet、decision、assistance、feedback、error 的关联和版本字段。

### 3.2 Core

- event → pattern → packet；正常交互不误报；
- quiet/idle、间隔、上限、历史“当前 + 前 3 次”；
- 多 Runtime、多 session、会话历史多 surface 和 focused timer lease；
- 后台 surface 交互后的关注转移；
- plan 替换、延迟 cleanup、suspend、deactivate 和 stale response；
- feedback 退避与 rejected/duplicate/mismatched 输入。

### 3.3 Adapter 与安全执行

- A2UI v0.8 等价 fixture；v0.9.1 必须补齐官方四类 message、扁平组件、basic/custom catalog conformance；
- standard/custom catalog、未知组件、selectionMode 和完整 owner；
- surface 快照重放、删除/不可达组件和裁剪标记；
- 默认空白名单、Host 显式授权、类型敏感值匹配；
- 多选、action、结构修改和未接受建议全部拒绝。

### 3.4 Service、Presenter 与 Transport

- Decision/Assistance 的 prompt 只作用于对应请求；
- 业务 Agent、独立模型和 Router 的路由不改变公共输出；
- invalid output、A2UI assistance、timeout、cancel 和错误映射；
- Presenter 实例隔离、替换、TTL、surface dismiss 和 stale 点击；
- accepted 只有在 Assistance/Executor 完成后才产生 outcome。

### 3.5 包、Host 与文档

- TypeScript build/test、npm tarball clean install 和禁止 deep import；
- Python test、sdist/wheel 与 clean install；
- `IIAP/` 子树独立复制后能够 build/test；
- JiuwenSwarm focused/system/typecheck/production build/browser E2E；
- 历史 surface 恢复、会话切换、网络/模型失败、文字帮助和 UI 更新测试路径；
- 文档链接、日期、包名、公共符号、JSON 示例及其中命令。

npm tarball 与 Python wheel 的 clean-install 不能只依赖人工历史记录。发布候选必须从
`IIAP/` 根目录运行 `npm run packages:smoke`：门禁在系统临时目录生成制品，分别安装到
全新 npm consumer 和 Python virtual environment，再通过已安装包名执行 import。临时
consumer 不得通过 workspace link、源码 `PYTHONPATH` 或仓库内 `node_modules` 代替制品。
tarball 检查还必须确认 `README.md`、许可证、声明文件和各受支持子入口已经入包。

测试命令和当前结果只在[集成测试与排障](../sdk-integration/05-demos-testing-and-troubleshooting.md)记录，避免设计正文保存会快速过期的运行结果。

## 4. JiuwenSwarm 迁移边界

JiuwenSwarm 回接使用 v0.8 Adapter 保持既有 A2UI 行为，并将嵌入式 singleton 替换为 Runtime/Session 实例。迁移应比较同一事件 fixture 产生的 observation plan、脱敏事件、pattern、packet 和 decision 安全行为。

迁移后仍属于 JiuwenSwarm Host 的能力包括 feature config、App/session/history 生命周期、Renderer registry、WebSocket/HTTP glue、业务 Agent 路由、日志、debug RPC 以及宿主旧数据形状兼容层。Runtime、观察窗口、pattern/policy、privacy、decision/assistance validator 和反馈状态机由 SDK 提供。JiuwenSwarm 兼容代码位于宿主仓库；`IIAP/` 内部不携带任何 JiuwenSwarm 专用 adapter，也不得反向 import `jiuwenswarm/...`。

生产路径不能保留隐藏的第二套 Runtime。回滚以嵌入式基线 tag 和 SDK 版本为边界，不通过运行时双写维持两套实现。

## 5. v0.1 发布门

正式 v0.1.0 必须同时满足：

- R-001～R-009 的设计结论与代码、公共导出、Schema、fixtures 和测试一致；
- SDK 子树独立构建，TypeScript、Python、Contract 和 package clean-install 验证通过；
- JiuwenSwarm 生产路径只运行 SDK Runtime，Host 回归和浏览器关键流程通过；
- SDK 设计文档和客户集成文档具有唯一索引、无重复权威定义，文档命令已经验证；
- 许可证、第三方依赖和发布内容清单完整；
- 用户确认后才把完整 `IIAP/` 子树导入 GitCode。

开发阶段不公开发布 npm/PyPI。正式包只从 GitCode 合入后的 `iiap-v0.1.0` tag 构建；GitHub 保留开发历史和验证过程，但不代替正式发布来源。
