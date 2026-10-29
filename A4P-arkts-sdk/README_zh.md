# A4P ArkTS SDK

[English](./README.md) | 简体中文

A4P（Agent-to-Agent Payment）支付意图协议的 HarmonyOS（ArkTS）实现，以 HAR 库（模块名
`a4p`）形式交付。覆盖带签名的意图生命周期（创建 → 签发 → 授权 → 激活）、基于冻结授权
快照的支付执行记账，以及用于签名与持久化的规范化 JSON（canonical JSON）编解码。密码学
通过 `Signer` / `SignatureVerifier` 接口由使用方注入。仓库中的 Java 移植版
[`A4P-java-sdk/`](../A4P-java-sdk) 即移植自本 SDK。

## 构建与测试

环境要求：DevEco Studio 5.0 及以上，HarmonyOS SDK `5.0.0(12)`（API 12）。命令行构建
还需要 `ohpm`、`hvigorw` 与 Bash 环境（Windows 上可用 Git Bash）；脚本会同时打包端侧
测试 HAP，该步骤需要 `java`（DevEco Studio 自带的 JBR 即可）。库本身没有第三方依赖
（`@ohos/hypium` 仅为测试期开发依赖）。

构建脚本总是先清理历史产物，再依次执行 `ohpm install`、`hvigor clean`、
`assembleHar`（release）与 `genOnDeviceTestHap`：

```sh
scripts/build.sh                                  # 从 PATH 解析 ohpm/hvigorw
scripts/build.sh --ohpm <path> --hvigorw <path>   # 或显式指定位置
```

产物：

- HAR 库：`a4p/build/default/outputs/default/a4p.har`
- 端侧测试 HAP：`a4p/build/default/outputs/ohosTest/a4p-ohosTest-unsigned.hap`

测试可在 DevEco Studio 中针对 `a4p/src/ohosTest` 运行，也可将打包好的测试 HAP 安装到
设备上运行。本 SDK 不带示例应用；测试源码兼作参考用法（见[测试](#测试)）。

### 引入本库

在使用方模块的 `oh-package.json5` 中添加依赖（源码路径或构建好的 HAR），然后执行
`ohpm install`：

```json5
{
  "dependencies": {
    "a4p": "file:../A4P-arkts-sdk/a4p"
  }
}
```

```typescript
import { A4p, A4pCodec, A4pErrorCode } from 'a4p';
```

所有公共内容都从唯一的 `Index.ets` 入口导出：`A4p` 与 `A4pCodec` 两个命名空间、
`Signer` / `SignatureVerifier` 接口，以及全部数据模型。

## API

| 规范 | ArkTS |
| --- | --- |
| `createIntent` | `A4p.createIntent(CreateIntentRequest, Signer[, now?])` 返回 `IntentL1` |
| `issueIntent` | `A4p.issueIntent(IntentL1, SignatureVerifier, IntentIssueContext, Signer[, now?])` 返回 `IntentL2` |
| `signIntent` | `A4p.signIntent(IntentL2, SignatureVerifier, SigningEnv, Signer[, now?])` 返回 `IntentL3` |
| `activateIntent` | `A4p.activateIntent(IntentL3, SignatureVerifier[, now?])` 返回 `IntentExecutionContext` |
| `verifyIntentAuthenticity` | `A4p.verifyIntentAuthenticity(IntentL3, SignatureVerifier)` 返回 `VerificationResult` |
| `revokeIntent` | `A4p.revokeIntent(IntentExecutionContext)` 返回 `IntentExecutionContext` |
| `createExecution / completeExecution / cancelExecution` | `A4p` 命名空间下的函数，返回 `IntentExecutionContext` |
| 失败上报 | 无结果包装：API 直接返回数据，失败时抛出符合 `BusinessError` 形态的错误，`code` 为 `A4pErrorCode`（具体类为模块内部实现，按结构捕获） |
| 编解码 | `A4pCodec.serializeIntentL1/L2/L3/IntentExecutionContext` 返回 `String`；对应的每个 `deserialize…` 先做结构校验再返回对象 |
| 密码学 | 不内置：`Signer.sign(Uint8Array): SignatureInfo` 与 `SignatureVerifier.verify(Uint8Array, SignatureInfo): VerificationResult` 由应用提供 |
| 数据模型 | 普通接口与字符串枚举（`AI_PAY`、`RSA3072`、`ACTIVE`、`PENDING`） |

`now?` 表示下文说明的可选显式时钟参数。

下面的片段展示调用形态；完整可编译的参考实现见 ohosTest 测试源码（尤其是
`A4p.test.ets` 与 `mock/TestDataFactory.ets`）。

```typescript
// Agent 创建并签名 L1；服务端验证后签发 L2 并绑定三方身份；用户验证 L2 后签名 L3。
const intentL1 = A4p.createIntent(request, agentSigner);
const intentL2 = A4p.issueIntent(intentL1, agentVerifier, issueContext, serverSigner);
const intentL3 = A4p.signIntent(intentL2, serverVerifier, signingEnv, userSigner);

// 激活：验证用户的 L3 签名，冻结授权快照。
let context = A4p.activateIntent(intentL3, userVerifier, now);

// 针对已激活的意图预留一笔订单，然后完成它。
context = A4p.createExecution(context, execution, now);
context = A4p.completeExecution(context, executionId, now);

// 失败以 BusinessError 形态抛出，按 code 分支处理。
try {
  context = A4p.createExecution(context, execution, now);
} catch (e) {
  const err = e as BusinessError<number>;
  if (err.code === A4pErrorCode.A4P_AMOUNT_EXCEEDED) { /* 额度已用尽 */ }
}
```

失败的 `createExecution` 仍可能已把 context 置为 `EXPIRED`（见下文的惰性翻转），因此
每次调用后都要持久化 context。`A4pCodec.serializeIntentExecutionContext` /
`deserializeIntentExecutionContext` 提供持久化形式；反序列化会重新校验存储的完整结构
——已授权的 L3 链、`intentId` 的推导、每一条账本记录（与状态绑定的终结时间戳、
`execution.id === executionId`、`periodIndex` 的有无与意图周期性一致）以及记账快照
（周期数值与 `leftExecutionCount` 恰在对应上限存在时出现）——并重新冻结授权快照。

可选字段用 `undefined` 表示；缺席的可选字段不会出现在签名 JSON 中。金额是应用约定的
最小货币单位下的正整数。所有协议数值都必须是 JavaScript 安全整数范围内的整数；不是
整数的值必须以字符串承载。传给 SDK 的值必须是普通对象（对象字面量），不能是类实例
——规范化序列化会以 `A4P_SERIALIZE_FAIL` 拒绝类实例。

生命周期与执行函数都接受可选的显式时钟（`now`，epoch 毫秒），缺省使用 `Date.now()`。
显式 `now` 必须是非负安全整数。测试中传入固定值可让签名字节可复现，也可用于实现
调用方的时钟偏移策略；SDK 自身不做任何时钟容差，生产代码必须从可信时钟取值，绝不能
使用客户端提供的时间戳。订单时间戳只被记录、不被评估：配额周期与有效期均以 `now`
判定。

必填字符串不能为空白（使用 JavaScript `trim()` 的空白字符集）。一个商品需要非空白的
seller id，且 `spuId`/`skuId` 至少提供一个非空白值；seller id + spuId + skuId 是白名单
匹配键，一个商品在每个分组内至多出现一次，且至多属于一个分组。周期规则要求
`anchorTime`、`periodSeconds`、`maxExecutionCountPerPeriod` 三者同时提供，且
`anchorTime <= validFrom`、`periodSeconds <= 9007199254740`。链上时间戳单调：
`L1.timestamp <= L2.timestamp <= L3.timestamp <= 激活时刻`。激活不做有效期窗口检查：
意图可以在 `validFrom` 之前（甚至 `validUntil` 之后）被激活；窗口在预留时才被强制，
早于窗口的预留以 `A4P_TIMESTAMP_TOO_EARLY` 失败。`intentConversation` 的长度限制与
敏感数据过滤由 Agent 负责。

### 结果码

调用方按数值 `code` 分支处理；消息是按码固定的诊断文本（具体原因在抛出点通过
hilog 记录）。取值来自 `A4pErrorCode` 枚举。`0` 表示成功，其他任何值都是协议失败。

| 码 | 常量 | 含义 |
| --- | --- | --- |
| 0 | `A4P_SUCCESS` | 操作成功 |
| 1001 | `A4P_INVALID_PARAMETER` | 参数无效 |
| 1002 | `A4P_UNSUPPORTED_VERSION` | 版本不受支持 |
| 1003 | `A4P_DUPLICATE_ID` | ID 重复 |
| 2001 | `A4P_SIGNING_FAIL` | 签名失败 |
| 2002 | `A4P_VERIFY_FAIL` | 验证失败 |
| 3001 | `A4P_INVALID_STATUS` | 状态无效 |
| 3002 | `A4P_TIMESTAMP_EXPIRED` | 时间戳已过期 |
| 3003 | `A4P_TIMESTAMP_TOO_EARLY` | 时间戳过早 |
| 4001 | `A4P_EXECUTION_COUNT_EXCEEDED` | 超出执行次数上限 |
| 4002 | `A4P_AMOUNT_EXCEEDED` | 超出金额上限 |
| 4003 | `A4P_PERIOD_COUNT_EXCEEDED` | 为码表兼容而定义；本 SDK 不返回此码（周期执行次数超限使用 4004） |
| 4004 | `A4P_PERIOD_EXECUTION_COUNT_EXCEEDED` | 超出周期执行次数上限 |
| 4005 | `A4P_CART_MISMATCH` | 购物车与授权资源不符 |
| 4006 | `A4P_SINGLE_ORDER_AMOUNT_EXCEEDED` | 超出单笔订单金额上限 |
| 4007 | `A4P_PRODUCT_NOT_AUTHORIZED` | 商品未被授权 |
| 5001 | `A4P_SERIALIZE_FAIL` | 序列化失败 |
| 5002 | `A4P_DESERIALIZE_FAIL` | 反序列化失败 |
| 6001 | `A4P_INTERNAL_ERROR` | 意外的内部错误 |

`2001` 与 `2002` 同样覆盖密码学边界：抛出异常、不返回结果或返回畸形结果的
`Signer`/`SignatureVerifier` 会被 SDK 映射为这两个码，原始异常不会穿透生命周期 API。

## 执行语义

- 待处理与已完成记录都占用金额、次数和周期配额；只有显式取消会释放配额，下游结果
  未知则不回滚。
- 购物车必须与白名单模板精确匹配：`cartItems` 与 `itemsWhiteList` 长度相同，每个购物车
  条目与一个不同的、列出该商品的分组配对（组内 OR，组间 AND），且数量等于该分组的
  `quantity`。购物车顺序无关。长度不符、一个分组被匹配两次、数量不符或有分组未覆盖，
  以 `A4P_CART_MISMATCH` 失败；商品不在任何分组中，以 `A4P_PRODUCT_NOT_AUTHORIZED`
  失败。
- `orderId` 是支付的幂等键：账本中已存在的订单 ID（含已取消记录）会以
  `A4P_DUPLICATE_ID` 被拒绝。重复的执行 ID 在取消之后依然被拒绝；调用方提供的执行 ID
  不能为空白，缺席时自动生成。
- 预留检查按固定顺序执行：请求结构；动作与币种是否与已授权意图一致；context 状态；
  有效期窗口（`validFrom`、`activatedTime` 下限、惰性置为 EXPIRED）；执行/订单 ID 重复；
  执行次数上限、单笔上限、剩余金额；白名单范围与模板；周期配额。
- `amount` 由调用方声明：SDK 只校验其不超过签名的金额上限，不与购物车对账（`Product`
  不含价格）。将金额绑定到真实价格是持有权威商品目录一方的职责。
- 撤销要求 context 处于 ACTIVE 并阻止新的预留，但待处理执行仍可完成或取消。
- 超过 `validUntil` 的预留会把 context 置为 EXPIRED，尽管该次调用失败；待处理记录在
  EXPIRED 与 REVOKED 状态下仍可被终结。
- 当一次完成操作之后不再有待处理记录，且要么达到次数上限、要么金额预算用尽时，ACTIVE
  的 context 变为 CONSUMED。REVOKED 与 EXPIRED 是终态。
- `executionInfo` 始终给出 `leftAmount`；`periodIndex`/`leftPeriodExecutionCount` 仅在
  周期意图上出现，`leftExecutionCount` 仅在存在 `maxExecuteCount` 时出现——不适用的数值
  被省略而非置 null。快照始终由账本推导，从不手工维护。
- 调用方的 execution 对象不会被改动：ID 在本地解析，且只在全部检查通过后写入账本自己的
  深拷贝。账本只追加。
- 完成操作不占用配额，但会按评估时刻重新推导记账快照，因此周期数值跟随 `now`，而不是
  最近一次账本变更。
- 已授权意图在激活时被深拷贝并深度冻结；执行检查读取不可变快照，而账本与数值保持
  可变。
- context 操作不加锁。这在 ArkTS 单线程运行时中是安全的，但**不**覆盖多个进程或同一个
  `intentId` 各自独立激活的副本。
- `verifyIntentAuthenticity` 校验 L3 链与服务端的 L2 签名，不校验用户签名，也不依赖任何
  context 状态。

账本位于内存中。接入方（意图中心）必须在以 `intentId` 加锁的数据库事务中持久化
context，防止同一个 `intentId` 被重放，并把密钥与身份绑定。

## 线格式与安全

每次签名背后的 canonical JSON 与 Base64URL 规则即
[canonical-json-spec.md](../A4P-java-sdk/docs/canonical-json-spec.md) 所记录的内容：键按
UTF-16 码元排序、无空白、仅限有限安全整数、`undefined` 省略、签名时仅移除顶层完整的
`sign` 属性（链上下层签名保留）、UTF-8 编码不带 BOM，以及 RFC 4648 无填充 Base64URL，
解码端拒绝填充、非 URL 安全字符与非零的未用尾随位。JSON 字段名即接口属性名
（`intentConversation`、`structuredIntent.resource`、`userIdentity.id`），每个枚举都按
名称序列化（`"AI_PAY"`、`"CONSUMED"`、`"PENDING"`），协议版本为 `1.0`。

每一层的签名覆盖该层除去整个 `sign` 对象之后的内容，因此该层自己的 `alg` 与
`keyIdentifier` 不在其自身签名字节内；它们由下一层的签名覆盖。这是规范规定的设计。
`alg` 必须是 `RSA3072`（因 `alg` 是分离元数据，结构校验即拒绝未知值）。验证方必须仅凭
`keyIdentifier` 在自己为该角色信任的密钥中解析，并拒绝与解析出的密钥类型不兼容的任何
`alg`——绝不要信任传入的 `alg`。这是防御算法/密钥混淆攻击的主要手段（同类问题参见
RFC 8725 的未签名 JWT 头部）。

SDK 不内置任何密码学实现。`RSA3072` 套件请基于 `@kit.CryptoArchitectureKit` 提供
`Signer`/`SignatureVerifier`，使用 `SHA256withRSA`（PKCS#1 v1.5）、3072 位密钥与无填充
Base64URL，以便与 Java SDK 的 `Rsa3072` 适配器互操作。

调用方提供的对象会被深拷贝进意图（绝不别名引用），激活后的快照被深度冻结。内部有一个
未导出的 `A4pHttp` 模块，为三个协议端点
（`/a4p/v1/intent/proposal|authorization|submission`，媒体类型 `application/a4p+json`）
构建与解析请求/响应封套，并把每个错误码映射到 HTTP 状态（签名失败 401、状态与配额
冲突 409、版本不受支持 426、参数错误 400、其余 500）；它有测试覆盖，但在规范确定绑定
之前不属于公共 API。

## 版本与兼容性

协议版本为 `1.0`。本 SDK 创建的每一层都写入该常量，每一步都会校验所收到的各层，不一致
（包括混合版本的链）时以 `A4P_UNSUPPORTED_VERSION` 拒绝。HAR 版本为 `1.0.0`；协议兼容
性由 `version` 字段决定，而不是由 HAR 版本决定。

Java 移植版（[`A4P-java-sdk/`](../A4P-java-sdk)）移植自本 SDK，与本文共享 canonical JSON
规则与错误码表，但它遵循 API 规范中更新的字段拼写（本 SDK 签名 `resource`，Java 侧为
`resources`；`Execution` 上多出显式的 `version` 字段）。因此两个 SDK 序列化出的意图并非
字节级一致；交换线格式数据前请先核对字段名。

## 已知限制

- 账本是内存内的、以单个 context 为粒度。持久化、加锁写入、防重放以及密钥与身份的绑定
  都由意图中心负责。
- 不内置密码学实现；`Signer`/`SignatureVerifier` 由应用提供。测试源码中基于 XOR 的
  `MockSigner` 只是确定性的测试替身，不得用于授权。
- HTTP 绑定为内部实现且未导出，待规范确定后再开放；公共能力仅限进程内调用与 canonical
  JSON 序列化。
- `verifyIntentAuthenticity` 有意不校验用户签名。
- 反序列化是结构性的（JSON.parse 语义）：不拒绝未知字段与重复 JSON 键，这一点不如
  Java 移植版严格。
- 激活不做有效期窗口检查；窗口在预留时强制（伴随惰性的 EXPIRED 翻转）。
- context 操作无同步保护：仅在 ArkTS 单线程运行时中安全，不覆盖多进程或各自独立激活的
  副本。

## 更多文档

| 文档 | 内容 |
| --- | --- |
| [canonical-json-spec.md](../A4P-java-sdk/docs/canonical-json-spec.md) | 每次签名背后的 canonical JSON 与 Base64URL 规则（本 SDK 实现的即该规则）。 |

## 测试

端侧 hypium 测试套件位于 `a4p/src/ohosTest`（300+ 用例）：

- `A4p.test.ets`——生命周期（创建/签发/签名/激活/核验/撤销）、空白与结构校验、签名器/
  验证器失败防护、执行记账、配额上限与回滚、订单幂等、购物车模板匹配、周期边界、检查
  顺序、过期终结与终态。
- `A4pCodec.test.ets`——确定性与键序无关的序列化、固定时钟下的 golden 线格式、往返
  编解码，以及 context 恢复（包括重新冻结授权快照）。
- `A4pHttp.test.ets`——封套构建与解析、完整的错误码 → HTTP 状态映射表，以及
  proposal → authorization → submission 全流程。
- `Utils.test.ets`——base64url、UTF-8 与 canonical JSON 的边界用例，包括对非规范编码的
  拒绝。

在 DevEco Studio 中针对 `a4p/src/ohosTest` 运行，或将构建出的测试 HAP 安装到设备上运行。
测试替身（`MockSigner`、`MockVerifier`、`TestDataFactory`）仅是确定性假实现，不是可用于
授权的密码学实现。
