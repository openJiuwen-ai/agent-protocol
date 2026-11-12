# Java SDK 公开 API

[English](API.md) | [简体中文](API_zh.md)

本文对应当前 `io.openjiuwen.a4p` 包的公开 API，要求 Java 17，支持 A4P Core `1.0`（`A4p.PROTOCOL_VERSION`）。当前支持的 action 类型为 `AI_PAY`。

## 导入与生命周期

```java
import io.openjiuwen.a4p.*;
import io.openjiuwen.a4p.Models.*;
```

协议 record 和枚举嵌套于 `Models`，仅导入 `A4p` 不会导入它们。`A4p` 提供静态生命周期方法，`PaymentProfile` 用于构造和解析支付载荷，`new A4pIntentCodec()` 提供规范 JSON 序列化与恢复能力。

```text
createIntent → issueIntent → signIntent → activateIntent
                                             ↓
                                      createExecution
                                             ↓
                             completeExecution / cancelExecution
```

各层 Intent 和执行 Context 应通过 SDK 接口创建或恢复；应用可自行构造请求、身份、环境和支付输入 record。禁止对同一个 Intent 或 Execution 重复调用相同的接口，管理生命周期的函数必须按顺序调用。同一个 Context 可以处理多个不同的 Execution。查询与序列化可重复调用。

SDK 负责授权预留和额度记账，应用负责实际对外支付，再根据支付结果完成或取消预留。activateIntent 调用后及每次更新后均须持久化 Context。同一意图的数据库读取、更新、写回应在事务中串行执行；单个内存 Context 的同步不能协调多个恢复副本或进程。预留失败也可能将传入 Context 改为 EXPIRED：即使失败结果没有 data，也须保存该 Context。

## 返回结果与异常

生命周期方法和反序列化方法返回 `A4pResult<T>`。先检查 `success()`，再读取 `data()`：成功时数据非空、结果码为 `0`；失败时数据为 null、结果码非零。业务判断应使用数值结果码。公开结果构造器在 success/data/code 不一致时抛出 `IllegalArgumentException`。

序列化、支付辅助方法和模型规范化可能抛出 `A4pException`，可通过 `code()` 读取 `A4pErrorCode`。返回结果的方法遇到非预期运行时缺陷时会抛出 `A4pException(A4P_INTERNAL_ERROR)`，而非返回普通失败结果。

`verifyIntentAuthenticity` 有两层结果：`result.success()` 表示 SDK 调用成功，`result.data().valid()` 才是签发方验签结论。可能存在即使函数返回成功，但是验签结果为非法的情况。

## 时间戳规则

所有时间戳和显式 `now` 参数均为 Unix epoch 毫秒。生命周期操作必须显式传入 `now`；显式时间必须来自可信时钟。协议数值为 ±9007199254740991 范围内的整数；时间戳非负，金额与数量为正整数。金额使用应用约定的货币最小单位。

时间戳的校验均在意图中心执行。意图中心需要提供可信任的时钟。下面是部分意图中心调用 API 的要求:

| 操作 | 时间要求 |
| --- | --- |
| `issueIntent` | 验签前要求 `now <= validUntil`，否则返回 `A4P_TIMESTAMP_EXPIRED`（3002）。不比较 `now` 与 `L1.timestamp`。 |
| `signIntent` | 仅校验时间戳格式，不比较 now 与 L2.timestamp 或 validUntil。 |
| `activateIntent` | `now <= validUntil`，否则返回过期；保存 `activatedTime = now`。 |
| `createExecution` | `validFrom <= now <= validUntil`；过早返回 3003，过期返回 3002，并可能将 Context 置为 EXPIRED。 |
| `completeExecution`、`cancelExecution` | `now >= reservedAt`，否则返回 3003；过期或撤销后仍可终结已有预留。 |

允许在 `validFrom` 前签名和激活，允许相同时间戳，有效期两端均包含在内。`Order.timestamp` 会作为时间戳校验，但不决定预留有效性或额度周期。读取 Context 状态不会自动判断是否过期。

代码中储存的时间戳分为两种，一种是仅作为审计用记录的时间戳，另一种是用于业务场景的时间戳。其中作为审计用的时间戳不会进行时间先后顺序的合法性校验。

下面的时间戳仅作为审计使用:
1. L1/L2/L3 `timestamp`
2. Context `activatedTime`/`revokedTime`

## 函数接口

| 类型 | 签名 | 说明 |
| --- | --- | --- |
| `A4p` | `static A4pResult<IntentL1> createIntent(CreateIntentRequest request, Signer agentSigner, long now)` | 创建并签名 L1；请求可省略 intentId，由 SDK 生成。将 `now` 用作 L1 时间戳，允许在 `validUntil` 之后创建。 |
| `A4p` | `static A4pResult<IntentL2> issueIntent(IntentL1 intent, SignatureVerifier agentVerifier, IntentIssueContext context, Signer serverSigner, long now)` | 校验 L1 和 Agent 签名，绑定身份后由服务端签发 L2。验签前要求 `now <= validUntil`，否则返回 `A4P_TIMESTAMP_EXPIRED`（3002）；不比较 now 与 L1.timestamp。 |
| `A4p` | `static A4pResult<IntentL3> signIntent(IntentL2 intent, SignatureVerifier serverVerifier, SigningEnv environment, Signer userSigner, long now)` | 验证 L2 服务端签名，绑定签名环境并生成用户签名的 L3。校验 L2 和内嵌 L1 的版本；仅校验时间戳格式，不比较 now 与 L2.timestamp 或 validUntil。允许在 `validFrom` 前提前签署；生效前预留由 `createExecution` 返回 `A4P_TIMESTAMP_TOO_EARLY`（3003）。 |
| `A4p` | `static A4pResult<IntentExecutionContext> activateIntent(IntentL3 intent, SignatureVerifier userVerifier, long now)` | 要求 `now <= validUntil`，再验证 L3 用户签名并创建 ACTIVE Context，记录 `activatedTime = now`。过期返回 `A4P_TIMESTAMP_EXPIRED`（3002）；不比较 now 与 L3.timestamp。 |
| `A4p` | `static A4pResult<VerificationResult> verifyIntentAuthenticity(IntentL3 intent, SignatureVerifier issuerVerifier)` | 验证意图链结构、版本和 L2 签发方签名，返回验证结果；不验证用户签名。 |
| `A4p` | `static A4pResult<IntentExecutionContext> revokeIntent(IntentExecutionContext context, long now)` | 将 ACTIVE Context 撤销为 REVOKED；已有 PENDING Execution仍可完成或取消。 |
| `A4p` | `static A4pResult<IntentExecutionContext> createExecution(IntentExecutionContext context, Execution execution, long now)` | 校验有效期、状态、唯一 ID、订单及额度，追加 PENDING 预留并刷新额度。 |
| `A4p` | `static A4pResult<IntentExecutionContext> completeExecution(IntentExecutionContext context, String executionId, long now)` | 完成指定 PENDING Execution，刷新当前周期 executionInfo；额度耗尽且无待处理Execution时变为 CONSUMED。 |
| `A4p` | `static A4pResult<IntentExecutionContext> cancelExecution(IntentExecutionContext context, String executionId, long now)` | 取消指定 PENDING Execution，释放其占用额度并刷新信息；账本记录保留。 |
| `A4pErrorCode` | `int code()` | 读取结果码或异常对应的错误码。 |
| `A4pErrorCode` | `String message()` | 读取诊断说明（SDK 生成的结果使用固定英文消息）。 |
| `A4pErrorCode` | `static A4pErrorCode fromCode(int code)` | 将整数结果码映射为枚举；未知值映射为内部错误。 |
| `A4pException` | `A4pException(A4pErrorCode code)` | 构建携带错误码的运行时异常，可提供消息与原因。 |
| `A4pException` | `A4pException(A4pErrorCode code, String message, Throwable cause)` | 构建携带错误码的运行时异常，可提供消息与原因。 |
| `A4pException` | `A4pErrorCode code()` | 读取结果码或异常对应的错误码。 |
| `A4pIntentCodec` | `A4pIntentCodec()` | 创建公开、可复用的规范 JSON 编解码器。 |
| `A4pIntentCodec` | `String serializeIntentL1(IntentL1 intentL1)` | 将 IntentL1 序列化为规范 JSON。 |
| `A4pIntentCodec` | `A4pResult<IntentL1> deserializeIntentL1(String serialized)` | 从本地 JSON 恢复并进行结构校验；不代替签名验证或防篡改检查。 |
| `A4pIntentCodec` | `String serializeIntentL2(IntentL2 intentL2)` | 将 IntentL2 序列化为规范 JSON。 |
| `A4pIntentCodec` | `A4pResult<IntentL2> deserializeIntentL2(String serialized)` | 从本地 JSON 恢复并进行结构校验；不代替签名验证或防篡改检查。 |
| `A4pIntentCodec` | `String serializeIntentL3(IntentL3 intentL3)` | 将 IntentL3 序列化为规范 JSON。 |
| `A4pIntentCodec` | `A4pResult<IntentL3> deserializeIntentL3(String serialized)` | 从本地 JSON 恢复并进行结构校验；不代替签名验证或防篡改检查。 |
| `A4pIntentCodec` | `String serializeIntentExecutionContext(IntentExecutionContext intentExecutionContext)` | 将 IntentExecutionContext 序列化为规范 JSON。 |
| `A4pIntentCodec` | `A4pResult<IntentExecutionContext> deserializeIntentExecutionContext(String serialized)` | 从本地 JSON 恢复并进行结构校验；不代替签名验证或防篡改检查。 |
| `A4pResult` | `boolean success()` | 读取操作成功标志。 |
| `A4pResult` | `T data()` | 读取成功数据；失败时为 null。 |
| `A4pResult` | `long code()` | 读取结果码或异常对应的错误码。 |
| `A4pResult` | `String message()` | 读取诊断说明. |
| `A4pResult` | `String toString()` | 返回包含 `success`、`code` 和 `message` 的结果摘要，省略 `data`。 |
| `A4pResult` | `A4pResult(boolean success, T data, long code, String message)` | 构造结果；成功要求数据非空且结果码为 0，失败要求数据为空且结果码非 0。 |
| `IntentExecutionContext` | `long activatedTime()` | 读取激活时的毫秒时间戳，序列化与恢复时保留。 |
| `IntentExecutionContext` | `Long revokedTime()` | 读取撤销时的 Unix 毫秒时间戳，缺少时间戳时返回 `null`。 |
| `IntentExecutionContext` | `String version()` | 读取 Context 保存的版本。 |
| `IntentExecutionContext` | `String intentId()` | 读取 Context 保存的意图标识。 |
| `IntentExecutionContext` | `IntentL3 authorizedIntent()` | 读取已授权 L3 快照。 |
| `IntentExecutionContext` | `synchronized IntentExecutionStatus status()` | 读取意图执行状态。 |
| `IntentExecutionContext` | `synchronized List<ExecutionRecord> executions()` | 读取只读账本快照。 |
| `IntentExecutionContext` | `synchronized JsonNode executionInfo()` | 读取额度快照；读取本身不会推进周期，执行接口调用会刷新。 |
| `PaymentProfile` | `static StructuredIntent structuredIntent(ShoppingItems resources, PaymentConstraints constraints)` | 将支付资源与约束封装为 AI_PAY 结构化意图。 |
| `PaymentProfile` | `static Execution execution(String id, Order order)` | 将订单封装为 AI_PAY 执行请求。 |
| `PaymentProfile` | `static ShoppingItems resources(StructuredIntent intent)` | 读取并解析 AI_PAY 资源。 |
| `PaymentProfile` | `static PaymentConstraints constraints(StructuredIntent intent)` | 读取并解析 AI_PAY 约束。 |
| `PaymentProfile` | `static Order order(Execution execution)` | 读取并解析Execution中的支付订单。 |
| `PaymentProfile` | `static PaymentExecutionInfo executionInfo(IntentExecutionContext context)` | 读取额度快照；读取本身不会推进周期，执行接口调用会刷新。 |
| `SignatureVerifier` | `VerificationResult verify(byte[] data, SignatureInfo signature) throws Exception` | 验证 SDK 提供的字节与签名，返回 VerificationResult。 |
| `Signer` | `SignatureInfo sign(byte[] data) throws Exception` | 对 SDK 提供的待签名字节签名并返回 SignatureInfo。 |

## 公开数据类型

下表类型均嵌套于 `io.openjiuwen.a4p.Models`，如 `io.openjiuwen.a4p.Models.IntentL1`。字段按规范构造器参数顺序列出；访问器与字段同名，例如 `order.amount()`。

| 类型 | 说明及字段 |
| --- | --- |
| `io.openjiuwen.a4p.Models.SellerInfo` | 商家标识; `String id` |
| `io.openjiuwen.a4p.Models.SpuSkuInfo` | 商品 SPU/SKU 标识; `String spuId, String skuId` |
| `io.openjiuwen.a4p.Models.Product` | 商家与商品标识; `SellerInfo seller, SpuSkuInfo spusku` |
| `io.openjiuwen.a4p.Models.CartItem` | 购物车商品与数量; `Product product, Long quantity` |
| `io.openjiuwen.a4p.Models.ItemWhiteList` | 允许的商品集合与数量; `List<Product> whiteList, Long quantity` |
| `io.openjiuwen.a4p.Models.ShoppingItems` | 支付资源白名单; `List<ItemWhiteList> itemsWhiteList` |
| `io.openjiuwen.a4p.Models.PaymentConstraints` | 金额、币种、有效期、次数与周期限制; `Long singleOrderMaxAmount, Long totalMaxAmount, String currency, Long validFrom, Long validUntil, Long maxExecutionCount, Long anchorTime, Long periodSeconds, Long maxExecutionCountPerPeriod` |
| `io.openjiuwen.a4p.Models.Order` | 订单标识、金额、币种、商品与时间戳; `String orderId, Long amount, String currency, List<CartItem> cartItems, Long timestamp` |
| `io.openjiuwen.a4p.Models.PaymentExecutionInfo` | 剩余额度与快照周期; `Long leftExecutionCount, long leftAmount, Long periodIndex, Long leftPeriodExecutionCount` |
| `io.openjiuwen.a4p.Models.StructuredIntent` | 动作及对应资源与约束; `IntentActionType action, JsonNode resources, JsonNode constraints` |
| `io.openjiuwen.a4p.Models.UserIdentity` | 用户身份; `String identityProvider, String id, String additionalInfo` |
| `io.openjiuwen.a4p.Models.AgentIdentity` | Agent 身份; `String identityProvider, String id, String additionalInfo` |
| `io.openjiuwen.a4p.Models.ServiceProviderIdentity` | 服务方身份; `String id, String additionalInfo` |
| `io.openjiuwen.a4p.Models.SigningEnv` | 签名设备与应用环境; `Map<String, Object> device, Map<String, Object> app` |
| `io.openjiuwen.a4p.Models.SignatureInfo` | 算法、密钥标识与签名; `String alg, String keyIdentifier, String sign` |
| `io.openjiuwen.a4p.Models.CreateIntentRequest` | 创建 L1 的输入; `String intentConversation, String intentSummary, StructuredIntent structuredIntent, String intentId` |
| `io.openjiuwen.a4p.Models.IntentIssueContext` | 签发 L2 的身份上下文输入; `UserIdentity userIdentity, AgentIdentity agentIdentity, ServiceProviderIdentity serviceProviderIdentity` |
| `io.openjiuwen.a4p.Models.VerificationResult` | 验证有效性、结果码与说明; `boolean valid, long code, String message` |
| `io.openjiuwen.a4p.Models.IntentL1` | Agent 创建的一级意图; `String version, String intentId, String intentConversation, String intentSummary, StructuredIntent structuredIntent, Long timestamp, SignatureInfo sign` |
| `io.openjiuwen.a4p.Models.IntentL2` | 服务端签发的二级意图; `String version, IntentL1 intentL1, UserIdentity userIdentity, AgentIdentity agentIdentity, ServiceProviderIdentity serviceProviderIdentity, Long timestamp, SignatureInfo sign` |
| `io.openjiuwen.a4p.Models.IntentL3` | 用户签名的三级意图; `String version, IntentL2 intentL2, SigningEnv signingEnv, Long timestamp, SignatureInfo sign` |
| `io.openjiuwen.a4p.Models.Execution` | 执行请求及动作内容; `IntentActionType action, String id, JsonNode executionContent` |
| `io.openjiuwen.a4p.Models.ExecutionRecord` | 预留、完成或取消的账本记录; `String executionId, ExecutionStatus status, IntentActionType action, JsonNode executionContent, Long periodIndex, long reservedAt, Long completedAt, Long canceledAt` |
| `io.openjiuwen.a4p.Models.IntentActionType` | `AI_PAY` |
| `io.openjiuwen.a4p.Models.AlgorithmType` | `RSA3072` |
| `io.openjiuwen.a4p.Models.IntentExecutionStatus` | `ACTIVE, CONSUMED, REVOKED, EXPIRED` |
| `io.openjiuwen.a4p.Models.ExecutionStatus` | `PENDING, COMPLETED, CANCELED` |

record 提供规范构造器和标准 `equals`、`hashCode`、`toString`。`SignatureInfo` 另有 `(AlgorithmType alg, String keyIdentifier, String sign)` 构造器；枚举提供 `values()` 和 `valueOf(String)`。集合会复制，环境 Map 会规范化并冻结，JSON 载荷访问器返回防御性副本。构造 record 不代替生命周期校验。`IntentExecutionContext` 无公开构造器，应通过激活或编解码器获取。

## 支付输入与购物车匹配

`PaymentProfile.structuredIntent(resources, constraints)` 和 `PaymentProfile.execution(id, order)` 将类型化 record 封装为按动作区分的 JSON，不代替生命周期校验。Java `null` 表示省略可选 record 字段；支付载荷解码器拒绝显式 JSON null，包括嵌套 null。

- `totalMaxAmount`、`currency`、`validFrom`、`validUntil` 必填，且 `validFrom <= validUntil`；`singleOrderMaxAmount` 和 `maxExecutionCount` 为可选正整数限制。订单币种必须与授权币种完全一致。
- `anchorTime`、`periodSeconds`、`maxExecutionCountPerPeriod` 必须一起提供或一起省略；`anchorTime <= validFrom`，`periodSeconds` 为正整数秒，最大为 9007199254740。不满足时返回 `A4P_INVALID_PARAMETER`。
- 白名单组及每组候选商品均不能为空。组间为 AND，每组候选商品为 OR。整个白名单中的商品必须唯一，同组内也不得重复。
- 每次执行必须为每组提供且仅提供一条购物车条目，数量与该组声明值完全相同。数量是单次执行模板，不是商品的累计额度。缺组、重复匹配同组或数量不符返回 `A4P_CART_MISMATCH`（4005）；未授权商品返回 `A4P_PRODUCT_NOT_AUTHORIZED`（4007），且先于购物车形状检查。
- 商品按完整 seller/SPU/SKU record 精确匹配。商家 ID 必填，SPU 和 SKU 至少提供一个非空白标识；省略某个标识不表示通配。
- 对话和摘要不能为空白；对话长度限制及敏感信息过滤由 Agent 应用负责。签发时须提供可信的用户、Agent 和服务方身份；签名时 device 与 app Map 均不可为 null。

## 状态与额度记账

| 状态 | 含义 |
| --- | --- |
| Context `ACTIVE` | 在时间和额度允许时接受新预留。 |
| Context `CONSUMED` | 总金额或总次数额度耗尽，且无 PENDING Execution；完成 ACTIVE Context 的最后一个待处理 Execution 时设置。 |
| Context `REVOKED` | 已主动撤销；拒绝新预留，允许完成或取消已有 PENDING Execution。 |
| Context `EXPIRED` | 由过期预留尝试设置；拒绝新预留，允许完成或取消已有 PENDING Execution。 |
| Execution `PENDING` | 占用金额、总次数及原预留周期的次数额度。 |
| Execution `COMPLETED` | 保留额度占用，不可再次终结。 |
| Execution `CANCELED` | 释放额度并保留记录，不可再次终结。 |

撤销接口 revokeIntent 要求 Context 为 ACTIVE。在 REVOKED 或 EXPIRED Context 中 completeExecution/cancelExecution 调用不会改变该 Context 状态。仅当周期次数耗尽时不会变成 CONSUMED。Execution ID 和 Order ID 在 Context 内唯一，取消后仍不可复用。Execution ID 为 null 时生成 UUID，可从更新后的账本读取。

周期(Period)下标为 `floor((now - anchorTime) / (periodSeconds * 1000))`。`ExecutionRecord.periodIndex` 在预留时确定，完成或取消不会改变其所属周期。PENDING 和 COMPLETED 占用额度，CANCELED 不占用。跨周期终结会刷新求值时所在周期的快照，但保留原预留周期及已完成Execution的全局用量；取消旧周期预留不会减少当前周期用量。

`executionInfo()` 是额度快照，在激活和成功预留、完成、取消时刷新。读取和撤销不会推进其周期。未配置的可选额度在 `PaymentExecutionInfo` 中为 null，在 Context JSON 中省略。

## 签名与验签回调

使用应用自身的密钥设施实现 `Signer` 和 `SignatureVerifier`。SDK 提供规范 UTF-8 JSON 字节：对象键排序、数组顺序保留、不含无意义空白。仅去除当前层最外侧 `sign` 字段，内嵌层签名仍被覆盖；应直接签署 SDK 提供的字节。

返回的 `SignatureInfo` 应包含 `alg`、可信密钥标识和无填充 Base64URL 签名。验签时应通过可信注册表解析密钥及其绑定算法；最外层签名元数据不被本层签名覆盖。所有签名回调异常（包括 `A4pException`）均映射为 `A4P_SIGNING_FAIL`。返回的签名元数据无效映射为 `A4P_INVALID_PARAMETER`。验签回调异常、返回 null 或 `valid` 与 `code == 0` 不一致映射为 `A4P_VERIFY_FAIL`。要求验签通过的生命周期方法也会将否定结论映射为 `A4P_VERIFY_FAIL`。

| API | 密码学验证 |
| --- | --- |
| `issueIntent` | L1 Agent 签名。 |
| `signIntent` | L2 意图中心签名。 |
| `activateIntent` | L3 用户签名。 |
| `verifyIntentAuthenticity` | 检查链结构和版本后验证 L2 签发方签名；不验证用户签名，也不判断当前能否执行。 |
| 反序列化方法 | 仅结构校验，不验证密码学签名。 |

可以参考 [序列化与签名规则](serialization_and_signature_rules_zh.md)

## 序列化与持久化

编解码器递归要求 JSON 数值为 `-9007199254740991` 至 `9007199254740991` 闭区间内的整数。序列化遇到 `1.0` 等浮点值或超出范围的整数时，抛出错误码为 `A4P_SERIALIZE_FAIL`（5001）的 `A4pException`。反序列化遇到浮点 token 或超出范围的整数时，返回 `A4P_DESERIALIZE_FAIL`（5002）。

`A4pIntentCodec` 是带公开无参构造器的公开具体类，可直接使用，无须应用自行实现或查找工厂。

```java
A4pIntentCodec codec = new A4pIntentCodec();
String json = codec.serializeIntentExecutionContext(context);
A4pResult<IntentExecutionContext> restored =
        codec.deserializeIntentExecutionContext(json);
if (!restored.success()) {
    throw new IllegalStateException("Restore failed: " + restored.code());
}
IntentExecutionContext restoredContext = restored.data();
```

编解码器支持 L1、L2、L3 和 Context。反序列化要求输入与对应序列化器的输出逐字符一致，额外的格式化空白、不同的字段顺序或等价的转义写法都会被拒绝。存储或转发时请保留原始序列化输出。已经重新排版的历史数据需要从原对象或可信副本通过对应的 SDK 序列化接口重新生成。

序列化在 Context 锁内取得一致快照。反序列化拒绝显式 JSON `null`、重复键和尾随 token，所有反序列化失败均返回 `A4P_DESERIALIZE_FAIL`（5002）。激活时分别使用 SDK 协议版本和已授权的 L1 意图标识初始化。反序列化时原样恢复这两个字符串以及持久化的账本和额度快照。

恢复不会认证持久化数据，也不会重新验签。需要时由应用保护存储完整性。Context JSON 仅用于本地持久化，不得作为网络消息。缺少必填 `activatedTime` 的旧 Context 无法直接恢复；应使用应用控制的可信迁移，或启动新的授权生命周期，同时避免重置已有授权用量。

## 结果码

| 枚举 | 数值 | 消息 |
| --- | --- | --- |
| `A4P_SUCCESS` | 0 | Operation completed successfully |
| `A4P_INVALID_PARAMETER` | 1001 | Invalid parameter |
| `A4P_UNSUPPORTED_VERSION` | 1002 | Unsupported version |
| `A4P_DUPLICATE_ID` | 1003 | Duplicate ID |
| `A4P_SIGNING_FAIL` | 2001 | Signing failed |
| `A4P_VERIFY_FAIL` | 2002 | Verification failed |
| `A4P_INVALID_STATUS` | 3001 | Invalid status |
| `A4P_TIMESTAMP_EXPIRED` | 3002 | Timestamp expired |
| `A4P_TIMESTAMP_TOO_EARLY` | 3003 | Timestamp too early |
| `A4P_EXECUTION_COUNT_EXCEEDED` | 4001 | Execution count exceeded |
| `A4P_AMOUNT_EXCEEDED` | 4002 | Amount exceeded |
| `A4P_PERIOD_COUNT_EXCEEDED` | 4003 | Period count exceeded |
| `A4P_PERIOD_EXECUTION_COUNT_EXCEEDED` | 4004 | Period execution count exceeded |
| `A4P_CART_MISMATCH` | 4005 | Cart does not match the authorized resource |
| `A4P_SINGLE_ORDER_AMOUNT_EXCEEDED` | 4006 | Single order amount exceeded |
| `A4P_PRODUCT_NOT_AUTHORIZED` | 4007 | Product not authorized |
| `A4P_SERIALIZE_FAIL` | 5001 | Serialization failed |
| `A4P_DESERIALIZE_FAIL` | 5002 | Deserialization failed |
| `A4P_INTERNAL_ERROR` | 6001 | Unexpected internal error |

4003 为兼容 ArkTS 保留，Java 使用 4004 表示周期执行次数超限。

## 版本校验与兼容性

接收 Intent 的生命周期接口以及 `verifyIntentAuthenticity` 要求传入的各层 Intent 都使用 SDK 协议版本。`revokeIntent`、`createExecution`、`completeExecution` 和 `cancelExecution` 要求 Context 版本及其授权 Intent 各层版本均与 SDK 一致。版本不受支持时返回 `A4P_UNSUPPORTED_VERSION`（1002）。版本不受支持的持久化 Context 仍可解码以供检查。恢复生命周期操作前，请使用支持该存储版本的 SDK。

## 使用约束

1. 所有 Intent 和 Context 结构必须通过 SDK 接口创建或恢复，禁止自行组装，否则可能产生未定义行为。请求、身份、环境与订单等输入模型可按下文构造。
2. 必须按生命周期顺序调用：createIntent → issueIntent → signIntent → activateIntent → createExecution → completeExecution 或 cancelExecution。只有处于 ACTIVE Context 才可调用 revokeIntent 接口。上述接口的输入 Intent/Context 必须是可信的。
3. 同一个 Intent 的同一生命周期步骤、同一个 Context 的激活/撤销步骤以及同一执行的预留/终结步骤不得重复调用。一个 ACTIVE Context 可处理多个不同执行；只读查询与序列化可以重复。
4. activateIntent 及其他返回 Context 的接口返回的 Context 对象需要被持久化储存。即使 createExecution 执行失败，也可能把状态改为 EXPIRED，此时仍须持久化当前 Context。持久化不是 SDK 自动完成的。
5. SDK 不负责持久化 Context 的防篡改或完整性验证；需要该能力时由调用方自行校验。反序列化中的格式检查不代表安全完整性校验。
6. 序列化 Context 只能用于本地持久化，不应作为消息通过网络传输。
7. 授权阶段（Intent 相关接口）不会校验 L1/L2/L3 Intent 创建时间戳的先后顺序，只作为审计使用
8. 签名/验签接口的实现需要确保其符合规范
9. 调用方需要校验 Intent/Context/Execution 的幂等，对于同一个 intentId ，其对应的 L1/L2/L3 Intent 以及 Context 都是唯一的。对于同一个 Context 而言，其中的 executionId 不能相同。
10. 防止并发调用接口
11. 用户必须确保序列化与反序列化接口的输入是可信的。输入不可信的结构/字符串到序列化/反序列化有可能出现没有报错，但是输出不可信结果的情况。
