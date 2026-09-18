# IIAP SDK 集成指南

最后更新：2026-08-06

版本：Integration Guide for IIAP v0.1.0

状态：源码候选已验证；npm/PyPI 尚未公开发布

本指南面向已经拥有 A2UI 生成、传输、解析和渲染能力，希望在现有系统中增加 IIAP 主动帮助能力的开发者。它回答“把 SDK 接在哪里、宿主要提供什么、怎样验证接入”，不重复定义协议或算法。

> 当前可使用源码、npm tarball 和 Python wheel 接入。即使正式版本只在 GitCode 发布源码，客户仍可从固定 tag 构建 `.tgz`/wheel，或通过 pip VCS 安装 Python 子目录；具体见[分步接入](03-step-by-step-integration.md#1-从-gitcode-源码发布安装-sdk)。公开 registry 不是使用 npm/pip 客户端的前提。GitCode 导入和正式版本发布目前尚未进行。

## 建议阅读路径

1. [端到端架构、公共边界与系统插入点](01-end-to-end-architecture-and-boundaries.md)：先确认已有业务/A2UI 模块如何连接 IIAP，以及客户真正需要感知的最小接口面。
2. [SDK 模块、接口与数据结构开发参考](02-sdk-modules-capabilities-and-interfaces.md)：逐模块确认功能、函数参数、返回值、默认实现、客户实现责任和关键数据结构。
3. [分步接入](03-step-by-step-integration.md)：按可运行的最小闭环实施。
4. [自定义 Adapter、配置与安全](04-custom-adapters-configuration-and-security.md)：适配自定义组件、模型路由和安全更新。
5. [Demo、测试与故障排查](05-demos-testing-and-troubleshooting.md)：完成验收并定位常见问题。
6. [兼容矩阵](compatibility-matrix.md)：确认 IIAP、A2UI、语言与交付物的适用范围。
7. [帮助类型与强触发配方](06-help-types-and-trigger-recipes.md)：需要让某一类帮助稳定出现，或排查"为什么只触发得出某一类"时查阅。

## 内容归属

| 问题 | 唯一权威位置 |
|---|---|
| 客户系统的端到端模块、公共接口边界和插入点 | [01](01-end-to-end-architecture-and-boundaries.md) |
| SDK 提供什么、函数如何调用、哪些由客户实现、数据如何流动 | [02 客户开发参考](02-sdk-modules-capabilities-and-interfaces.md) |
| 如何按步骤完成接入 | [03](03-step-by-step-integration.md) |
| 自定义组件、配置和安全规则 | [04](04-custom-adapters-configuration-and-security.md) |
| Demo、验收结果和排障 | [05](05-demos-testing-and-troubleshooting.md) |
| 帮助类型清单、触发链路与强触发配方 | [06](06-help-types-and-trigger-recipes.md) |
| 版本兼容性 | [兼容矩阵](compatibility-matrix.md) |
| SDK 产品边界、wire protocol、数据结构和算法 | [SDK 设计文档](../sdk-design/README.md) |

## 接入完成的定义

一次完整接入应同时满足：A2UI surface 能建立观察计划；Renderer 事件能准确路由到对应 surface；SDK 能产生脱敏 packet 并获得相关联的决策；用户可以接受或拒绝帮助；文字帮助不伪装成用户消息；UI 更新只有在明确接受且通过双端白名单校验后才能执行；用户反馈和 surface 生命周期能够闭环。
