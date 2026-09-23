# 测试与兼容性

最后更新：2026-09-20

## 1. 支持范围

| 范围 | v0.1.0-rc.1 |
|---|---|
| IIAP wire | 0.1 |
| A2UI 默认 Adapter | v0.8 |
| A2UI v0.9.1 | wire version 可表示；无默认 Adapter |
| Node.js | 20+ |
| Python | 3.11+ |
| React Presenter | React 18.2 可选 peer |
| 网络 | 默认 HTTP；可实现自定义 Transport |

## 2. 验证命令

从 IIAP/ 执行：

~~~bash
npm test
npm run contracts
npm run docs
npm run packages:smoke
npm run demo:build

uv run --project implementations/python pytest implementations/python/tests -q
npx tsc -p implementations/typescript/tsconfig.json --noEmit --noUnusedLocals --noUnusedParameters
~~~

packages:smoke 会构建真实 npm tarball 和 Python wheel，在全新临时消费者中安装并导入
所有正式子入口，同时检查 wheel 内置 packet Schema。

## 3. 覆盖范围

- TypeScript：Runtime、Adapter、owner、surface、timer、history、decision、feedback、privacy、
  suggestion、HTTP、React Presenter 和公共 API snapshot。
- Python：packet Schema、privacy、prompt、decision/assistance service、A2UI v0.8 codec 和
  update allowlist。
- Contract：canonical fixture、负向 shape、跨语言 assistance corpus。
- 制品：npm/Python clean install 和 Demo build。

## 4. 发布门

- 所有命令退出码为 0；
- npm exports 与 public API snapshot 一致；
- tarball/wheel 不包含未公开 prototype 或宿主专用代码；
- 中英文文档链接和日期检查通过；
- git diff --check 通过且没有生成物进入 git；
- 固定源码 commit、VERSION、Changelog 和制品 metadata 一致；
- 第三方依赖清单完成项目合规复核。

## 5. 仍需宿主验收

SDK 自动化不证明以下内容：

- 真实模型在 production/test 下的帮助质量；
- 浏览器 DOM 语义事件是否完整；
- Host 自定义 Presenter 的非阻塞、键盘和读屏行为；
- 接受 update 后真实 A2UI data model 是否原子变化；
- WebSocket/RPC 关联、历史恢复和业务 Agent 集成；
- 自定义 A2UI catalog 或 v0.9.1 conformance。

组合测试工程必须记录匹配的 SDK commit 和 IIAP tree hash，并逐项验证上述链路。
