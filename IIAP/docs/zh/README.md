# IIAP SDK 中文文档

最后更新：2026-09-18

本页是 IIAP SDK 中文文档的唯一对外入口。当前交付物为 `0.1.0-rc.1` 源码候选；源码构建、本地 npm tarball、Python wheel 和 JiuwenSwarm 回接已经过自动化验证，但 npm/PyPI 尚未公开发布，也尚未导入 GitCode。英文首次接入见 [English documentation](../en/README.md)。

## 按读者选择入口

| 读者目标 | 从这里开始 | 最终得到什么 |
|---|---|---|
| 快速理解 IIAP 的背景、价值和整体方案 | [IIAP 价值与方案概览](00-value-and-solution-overview.md) | A2UI 的价值与不足、IIAP 挑战、架构、工作流、协议和模块设计 |
| 评审 SDK 产品与技术方案 | [SDK 设计文档](sdk-design/README.md) | 产品边界、模块架构、wire contract、算法、安全和兼容策略 |
| 把 IIAP 接入已有 A2UI 系统 | [SDK 集成指南](sdk-integration/README.md) | 端到端模块地图、北向接口、接入步骤、Demo、测试和排障方法 |
| 维护或修改本文档体系 | [文档维护规则](documentation-maintenance.md) | 权威来源、同步、去重、归档和验证要求 |

首次接入建议直接按照集成指南的顺序阅读。需要理解字段或算法时，再跳转到设计文档对应的唯一权威章节。

## 内容归属

| 主题 | 唯一权威位置 |
|---|---|
| 宏观价值、方案全景与非规范性导读 | [IIAP 价值与方案概览](00-value-and-solution-overview.md) |
| 目标、边界、概念、模块依赖与架构 | [设计 01](sdk-design/01-overview-architecture-and-concepts.md) |
| TypeScript/Python 公共 API、参数、返回值、默认实现与客户实现责任 | [客户开发参考](sdk-integration/02-sdk-modules-capabilities-and-interfaces.md) |
| Wire envelope、packet、decision、assistance、feedback 与字段关联 | [设计 02](sdk-design/02-wire-protocol-and-data-contracts.md) |
| 观察、聚合、surface、prompt、帮助、反馈、隐私与安全 | [设计 03](sdk-design/03-runtime-observation-assistance-and-security.md) |
| Adapter、兼容性、测试、版本、迁移和发布门 | [设计 04](sdk-design/04-adapters-compatibility-testing-and-release.md) |
| 客户端到端架构、业务/A2UI/IIAP 边界、公共接口与插入点 | [集成 01](sdk-integration/01-end-to-end-architecture-and-boundaries.md) |
| 接入步骤、自定义、安全配置、Demo 与排障 | [集成指南](sdk-integration/README.md) |
| 版本支持范围 | [兼容矩阵](sdk-integration/compatibility-matrix.md) |
| 帮助类型清单、从页面交互到帮助类型的完整映射、各类强触发配方与"触发不了"的排查顺序 | [帮助类型与强触发配方](sdk-integration/06-help-types-and-trigger-recipes.md) |

JiuwenSwarm 的嵌入式历史、迁移审计、内部决策记录和开发状态属于宿主测试仓，
不随独立 SDK 发布，也不定义 SDK 公共行为。

## 使用口径

- `IIAP/VERSION`、公共导出、JSON Schema、源码和通过的测试共同定义当前源码候选。
- 文档出现“正式包”或 registry 安装命令时，必须明确标记为发布后的目标形态。
- IIAP 是插件能力，不替代业务 Agent 或 A2UI；用户必须明确接受后才能执行安全 UI 更新。
- 所有 IIAP 文档修改必须遵循[文档维护规则](documentation-maintenance.md)。
