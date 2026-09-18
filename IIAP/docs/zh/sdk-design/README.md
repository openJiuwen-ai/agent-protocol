# IIAP SDK 设计文档

最后更新：2026-08-06

状态：IIAP v0.1.0 源码发布候选；npm/PyPI 尚未公开发布

本目录是通用 IIAP SDK 产品边界、wire contract 和运行规则的权威来源。它回答“SDK 是什么、为什么这样设计、跨进程契约和运行规则是什么”。公共函数如何调用、哪些接口由客户实现以及如何把 SDK 接入现有系统，由[SDK 集成文档](../sdk-integration/README.md)说明；JiuwenSwarm 的历史实现、迁移证据和开发状态保存在仓库 `docs/zh/iiap/`，不定义 SDK 公共契约。

## 阅读顺序与内容归属

| 主题 | 唯一权威文档 |
|---|---|
| 产品目标、边界、概念、模块、依赖方向和端到端数据流 | [01 概述、架构与关键概念](01-overview-architecture-and-concepts.md) |
| Wire envelope、packet、decision、assistance、feedback 和字段关联 | [02 Wire 协议与数据契约](02-wire-protocol-and-data-contracts.md) |
| 观察聚合、surface 并发、提示词、帮助生命周期、隐私与安全更新 | [03 Runtime、观察、帮助与安全](03-runtime-observation-assistance-and-security.md) |
| A2UI Adapter、兼容性、版本策略、测试、发布门和 JiuwenSwarm 迁移 | [04 Adapter、兼容、测试与发布](04-adapters-compatibility-testing-and-release.md) |

## 设计状态与使用口径

- R-001～R-009 已完成产品评审；设计结论已收敛到上述四篇文档，函数级公共 API 结论收敛到统一的客户开发参考。
- `IIAP/VERSION`、公共导出、JSON Schema 与通过的测试共同定义当前源码候选行为。
- TypeScript/Python 公共 API 的统一开发参考位于[SDK 模块、接口与数据结构开发参考](../sdk-integration/02-sdk-modules-capabilities-and-interfaces.md)，本目录不重复维护函数级签名和用法。
- `@openjiuwen/iiap` 和 `openjiuwen-iiap` 是目标包名；registry 安装命令在正式发布前仅用于说明目标形态。
- 文档中的 Core 子模块是逻辑职责，不表示客户需要分别安装多个包。
- 同一 wire contract 或算法只在上表指定的位置定义，其他文档只能给出接入摘要并链接到本目录。
