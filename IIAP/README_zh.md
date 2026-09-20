# IIAP SDK

IIAP（智能交互辅助协议）从 UI 语义事件中识别隐私安全的交互模式，并提供可选、非阻塞的帮助。本目录可独立构建，并可原样迁移至 `agent-protocol/IIAP`。

当前版本：`0.1.0-rc.1`

## 内容入口

- [中文文档总入口](docs/zh/README.md)
- [快速开始](docs/zh/quickstart.md)
- [API 参考](docs/zh/api-reference.md)
- [协议与安全](docs/zh/protocol-and-security.md)
- [测试与兼容性](docs/zh/testing-and-compatibility.md)
- [第三方依赖技术清单](THIRD_PARTY_DEPENDENCIES.md)
- TypeScript 单一 npm 包：`@openjiuwen/iiap`，构建入口为 `implementations/typescript/packages/core`
- TypeScript 子入口：`/decision`、`/http`、`/a2ui-v08`、`/react`、`/testing`
- Python Service：`implementations/python`
- 源码逻辑模块：正式支持的 A2UI v0.8 Adapter 位于 `adapters/`，HTTP Transport 位于 `transports/http`，可选 React Presenter 位于 `presenters/react`

SDK 不自动提交表单，也不会在用户明确接受前执行建议。反馈上传为可选能力，默认关闭。
Runtime 必须在 Host 托管 `onPacket` 与 SDK 托管 `transport` 中二选一。

分发本地制品前运行 `npm run packages:smoke`。该命令会构建 TypeScript SDK，
在全新临时项目中安装并导入 npm tarball，同时构建 Python wheel，
在全新虚拟环境中安装并导入 Python 包。
