# 兼容矩阵

最后更新：2026-09-18

## A2UI 协议

| IIAP | A2UI wire version | TypeScript 入口 | 当前验证范围 | 推荐用途 |
|---|---|---|---|---|
| 0.1.0-rc.1 | 0.8 | `@openjiuwen/iiap/a2ui-v08` | Adapter 测试、完整 Demo 及 JiuwenSwarm 回接 | 当前唯一正式支持路径 |
| 0.1.0-rc.1 | 0.9.1 | 未提供公共入口 | 仅保留未发布的源码 prototype；官方扁平组件 fixture 失败 | 不用于生产；等待 conformance 或实现 Custom Adapter |
| 0.1.x | 其他版本 | 未提供 | 未验证 | 实现自定义 Adapter 或等待正式支持 |

不要只根据组件外观选择 Adapter；以系统实际收发的 A2UI wire message 版本为准。目标上，不同版本 Adapter 应输出相同 IIAP 公共契约并生成各自协议格式的安全更新；当前只有 v0.8 达到这一要求。

## 语言与运行环境

| 能力 | 当前参考实现 | 用途 | 状态 |
|---|---|---|---|
| Runtime、A2UI Adapter、Transport、Presenter、Executor | TypeScript；Node.js 20+ | UI/端侧集成 | 源码与本地包已验证 |
| DecisionService、AssistanceService、ModelAdapter | Python 3.11+；参考 `uv` | 模型侧服务 | 源码、sdist、wheel 已验证 |
| React Presenter | React 18.2 peer dependency | 默认 Web 帮助 UI | 可选；非 React 系统实现 HelpPresenter |
| ArkTS/HarmonyOS | 无参考包 | 移动端目标 | 尚未验证；可复用语言中立 Schema |
| C++ | 无参考包 | 原生目标 | 尚未实现 |

## 交付方式

| 方式 | 当前状态 | 说明 |
|---|---|---|
| 完整 `IIAP/` 源码 | 可用 | 可以独立复制、构建和测试，是当前推荐的客户评审与接入方式 |
| 本地 npm tarball | 已验证 | 单包 `@openjiuwen/iiap`，含可选子路径 |
| Python sdist/wheel | 已验证 | distribution 为 `openjiuwen-iiap`，import 为 `iiap` |
| pip GitCode VCS + `subdirectory` | 正式源码 tag 可用后支持 | 可直接安装 Python 子目录；必须固定 tag/commit，并具备 Git 和构建环境 |
| npm GitCode VCS 直装 | 不支持当前目录布局 | npm 无法可靠选择 monorepo 内的 TypeScript package 并预构建 `dist`；应安装源码构建的 `.tgz` |
| npm/PyPI registry | 未发布 | GitCode 导入、正式评审和 tag 完成后发布 |
| GitCode `agent-protocol/IIAP` | 未导入 | 需完成最终验证并获得明确确认 |
