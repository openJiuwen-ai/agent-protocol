# Java SDK 公开 API

[English](API.md) | [简体中文](API_zh.md)

## 使用约束

1. 所有 Intent 和 Context 结构必须通过 SDK 接口创建或恢复，禁止自行组装，否则可能产生未定义行为。请求、身份、环境与订单等输入模型可按下文构造。
2. 必须按生命周期顺序调用：createIntent → issueIntent → signIntent → activateIntent → createExecution → completeExecution 或 cancelExecution；可撤销 ACTIVE Context。
3. 同一个 Intent 的同一生命周期步骤、同一个 Context 的激活/撤销步骤以及同一执行的预留/终结步骤不得重复调用。一个 ACTIVE Context 可处理多个不同执行；只读查询与序列化可以重复。
4. activateIntent 及其他返回 Context 的接口返回后必须持久化 Context。createExecution 失败时也可能把状态改为 EXPIRED，仍须持久化当前 Context。持久化不是 SDK 自动完成的。
5. SDK 不负责持久化 Context 的防篡改或完整性验证；需要该能力时由调用方自行校验。反序列化中的格式检查不代表安全完整性校验。
6. 序列化 Context 只能用于本地持久化，不应作为消息通过网络传输。

## 时间与兼容性

所有时间戳和 now 都为 Unix epoch 毫秒。activatedTime 是激活调用采样的时间，Java 为 long，ArkTS 为安全整数 number。执行创建、完成、取消和撤销均拒绝早于 activatedTime 的调用，允许同一毫秒调用。可选 now 省略时读取系统时间，显式提供时用作当前求值时间。完成周期执行时按完成时间刷新 executionInfo，不改变预留记录所属周期。新增字段必填，缺少 activatedTime 的旧 Context 无法直接恢复；请重新激活或由应用制定可信迁移方案。

Java 生命周期 API 返回 A4pResult<T>：检查 success() 后读取 data()；签发、签名和激活时间早于前一层意图时间戳时返回 A4P_INVALID_PARAMETER；执行预留或撤销早于 activatedTime，以及完成或取消早于 reservedAt 时返回 A4P_TIMESTAMP_TOO_EARLY。序列化与部分工具 API 抛 A4pException。无 now 的重载使用 System.currentTimeMillis()。

## 函数接口

| 类型 | 签名 | 说明 |
| --- | --- | --- |
| `A4p` | `static A4pResult<IntentL1> createIntent(CreateIntentRequest request, Signer agentSigner)` | 创建并签名 L1；请求可省略 intentId，由 SDK 生成。 |
| `A4p` | `static A4pResult<IntentL1> createIntent(CreateIntentRequest request, Signer agentSigner, long now)` | 创建并签名 L1；请求可省略 intentId，由 SDK 生成。 |
| `A4p` | `static A4pResult<IntentL2> issueIntent(IntentL1 intent, SignatureVerifier agentVerifier, IntentIssueContext context, Signer serverSigner)` | 校验 L1 和 Agent 签名，绑定身份后由服务端签发 L2。 |
| `A4p` | `static A4pResult<IntentL2> issueIntent(IntentL1 intent, SignatureVerifier agentVerifier, IntentIssueContext context, Signer serverSigner, long now)` | 校验 L1 和 Agent 签名，绑定身份后由服务端签发 L2。 |
| `A4p` | `static A4pResult<IntentL3> signIntent(IntentL2 intent, SignatureVerifier serverVerifier, SigningEnv environment, Signer userSigner)` | 验证 L2 服务端签名，绑定签名环境并生成用户签名的 L3。校验 L2 和内嵌 L1 的版本；要求 `now >= L2.timestamp`（否则返回 `A4P_INVALID_PARAMETER`）且 `now <= validUntil`（否则返回 `A4P_TIMESTAMP_EXPIRED`，3002），时间校验先于验签和用户签名。允许在 `validFrom` 前提前签署；生效前预留由 `createExecution` 返回 `A4P_TIMESTAMP_TOO_EARLY`（3003）。省略 `now` 时使用系统时间。 |
| `A4p` | `static A4pResult<IntentL3> signIntent(IntentL2 intent, SignatureVerifier serverVerifier, SigningEnv environment, Signer userSigner, long now)` | 验证 L2 服务端签名，绑定签名环境并生成用户签名的 L3。校验 L2 和内嵌 L1 的版本；要求 `now >= L2.timestamp`（否则返回 `A4P_INVALID_PARAMETER`）且 `now <= validUntil`（否则返回 `A4P_TIMESTAMP_EXPIRED`，3002），时间校验先于验签和用户签名。允许在 `validFrom` 前提前签署；生效前预留由 `createExecution` 返回 `A4P_TIMESTAMP_TOO_EARLY`（3003）。 |
| `A4p` | `static A4pResult<IntentExecutionContext> activateIntent(IntentL3 intent, SignatureVerifier userVerifier)` | 验证 L3 用户签名并创建 ACTIVE Context，记录 activatedTime。 |
| `A4p` | `static A4pResult<IntentExecutionContext> activateIntent(IntentL3 intent, SignatureVerifier userVerifier, long now)` | 验证 L3 用户签名并创建 ACTIVE Context，记录 activatedTime。 |
| `A4p` | `static A4pResult<VerificationResult> verifyIntentAuthenticity(IntentL3 intent, SignatureVerifier issuerVerifier)` | 验证意图链结构、版本和 L2 签发方签名，返回验证结果；不验证用户签名。 |
| `A4p` | `static A4pResult<IntentExecutionContext> revokeIntent(IntentExecutionContext context)` | 将 ACTIVE Context 撤销为 REVOKED；已有 PENDING 执行仍可完成或取消。 |
| `A4p` | `static A4pResult<IntentExecutionContext> revokeIntent(IntentExecutionContext context, long now)` | 将 ACTIVE Context 撤销为 REVOKED；已有 PENDING 执行仍可完成或取消。 |
| `A4p` | `static A4pResult<IntentExecutionContext> createExecution(IntentExecutionContext context, Execution execution)` | 校验有效期、状态、唯一 ID、订单及额度，追加 PENDING 预留并刷新额度。 |
| `A4p` | `static A4pResult<IntentExecutionContext> createExecution(IntentExecutionContext context, Execution execution, long now)` | 校验有效期、状态、唯一 ID、订单及额度，追加 PENDING 预留并刷新额度。 |
| `A4p` | `static A4pResult<IntentExecutionContext> completeExecution(IntentExecutionContext context, String executionId)` | 完成指定 PENDING 执行，刷新当前周期 executionInfo；额度耗尽且无待处理执行时变为 CONSUMED。 |
| `A4p` | `static A4pResult<IntentExecutionContext> completeExecution(IntentExecutionContext context, String executionId, long now)` | 完成指定 PENDING 执行，刷新当前周期 executionInfo；额度耗尽且无待处理执行时变为 CONSUMED。 |
| `A4p` | `static A4pResult<IntentExecutionContext> cancelExecution(IntentExecutionContext context, String executionId)` | 取消指定 PENDING 执行，释放其占用额度并刷新信息；账本记录保留。 |
| `A4p` | `static A4pResult<IntentExecutionContext> cancelExecution(IntentExecutionContext context, String executionId, long now)` | 取消指定 PENDING 执行，释放其占用额度并刷新信息；账本记录保留。 |
| `A4pErrorCode` | `int code()` | 读取结果码或异常对应的错误码。 |
| `A4pErrorCode` | `String message()` | 读取错误码对应的固定说明。 |
| `A4pErrorCode` | `static A4pErrorCode fromCode(int code)` | 将整数结果码映射为枚举；未知值映射为内部错误。 |
| `A4pException` | `A4pException(A4pErrorCode code)` | 构建携带错误码的运行时异常，可提供消息与原因。 |
| `A4pException` | `A4pException(A4pErrorCode code, String message, Throwable cause)` | 构建携带错误码的运行时异常，可提供消息与原因。 |
| `A4pException` | `A4pErrorCode code()` | 读取结果码或异常对应的错误码。 |
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
| `A4pResult` | `String message()` | 读取错误码对应的固定说明。 |
| `A4pResult` | `String toString()` | 返回不包含业务数据的结果摘要。 |
| `A4pResult` | `A4pResult(boolean success, T data, long code, String message)` | 供应用编解码器构造结果；成功要求数据非空且结果码为 0，失败要求数据为空且结果码非 0。 |
| `IntentExecutionContext` | `long activatedTime()` | 读取激活时的毫秒时间戳，序列化与恢复时保留。 |
| `IntentExecutionContext` | `String version()` | 读取协议版本。 |
| `IntentExecutionContext` | `String intentId()` | 读取关联意图标识。 |
| `IntentExecutionContext` | `IntentL3 authorizedIntent()` | 读取已授权 L3 快照。 |
| `IntentExecutionContext` | `synchronized IntentExecutionStatus status()` | 读取意图执行状态。 |
| `IntentExecutionContext` | `synchronized List<ExecutionRecord> executions()` | 读取只读账本快照。 |
| `IntentExecutionContext` | `synchronized JsonNode executionInfo()` | 读取额度快照；读取本身不会推进周期，执行接口调用会刷新。 |
| `PaymentProfile` | `static StructuredIntent structuredIntent(ShoppingItems resources, PaymentConstraints constraints)` | 将支付资源与约束封装为 AI_PAY 结构化意图。 |
| `PaymentProfile` | `static Execution execution(String id, Order order)` | 将订单封装为 AI_PAY 执行请求。 |
| `PaymentProfile` | `static ShoppingItems resources(StructuredIntent intent)` | 读取并解析 AI_PAY 资源。 |
| `PaymentProfile` | `static PaymentConstraints constraints(StructuredIntent intent)` | 读取并解析 AI_PAY 约束。 |
| `PaymentProfile` | `static Order order(Execution execution)` | 读取并解析执行中的支付订单。 |
| `PaymentProfile` | `static PaymentExecutionInfo executionInfo(IntentExecutionContext context)` | 读取额度快照；读取本身不会推进周期，执行接口调用会刷新。 |
| `SignatureVerifier` | `VerificationResult verify(byte[] data, SignatureInfo signature) throws Exception` | 验证 SDK 提供的字节与签名，返回 VerificationResult。 |
| `Signer` | `SignatureInfo sign(byte[] data) throws Exception` | 对 SDK 提供的待签名字节签名并返回 SignatureInfo。 |

## 公开数据类型

Java 的协议 record 与枚举嵌套在 `io.openjiuwen.a4p.Models` 中。下表下表逐一列出完整引用路径，例如 `io.openjiuwen.a4p.Models.IntentL1`；也可使用 `import io.openjiuwen.a4p.Models.*;`。`import io.openjiuwen.a4p.A4p` 仅导入生命周期类，不能导入这些嵌套类型。`PaymentProfile` 明确保留为构造和解析 AI_PAY 载荷的公开便利 API。

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

所有 record 公开规范构造器、同名字段访问器及标准 equals/hashCode/toString。这些成员按字段名读取上述数据；IntentL1/L2/L3 的公开构造器仅用于类型表达，不授权绕过 SDK 生命周期。集合会复制；JSON 通过防御性复制读取。SignatureInfo 另有接受 AlgorithmType 的构造器。PROTOCOL_VERSION 为 "1.0"，MAX_SAFE_INTEGER 为 9007199254740991。A4pErrorCode 的所有枚举值及说明见源码；枚举提供标准 values()/valueOf(String)。

## 编解码实现与持久化

应用需自行实现并持有 `A4pIntentCodec`。SDK 标准编解码器仅包内可见，接口不提供公开工厂函数。源码中的测试程序 `CodecConformanceCheck` 使用代表性的 L1、L2、L3 和 Context 数据，与内部标准实现对照；通过这些样例不代表完整兼容性证明。

将实现放入测试类路径后，从仓库根目录运行：

```sh
mvn -f A4P-java-sdk/pom.xml test-compile org.codehaus.mojo:exec-maven-plugin:3.6.3:java -Dexec.classpathScope=test -Dexec.mainClass=io.openjiuwen.a4p.CodecConformanceCheck -Dexec.args=your.package.YourIntentCodec
```

```java
A4pIntentCodec codec = new MyIntentCodec(); // 应用自行实现
String json = codec.serializeIntentExecutionContext(context);
A4pResult<IntentExecutionContext> result = codec.deserializeIntentExecutionContext(json);
```

## 结果码

| 枚举 | 数值 | 说明 |
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
| `A4P_PRODUCT_QUANTITY_EXCEEDED` | 4005 | Product quantity exceeded |
| `A4P_SINGLE_ORDER_AMOUNT_EXCEEDED` | 4006 | Single order amount exceeded |
| `A4P_PRODUCT_NOT_AUTHORIZED` | 4007 | Product not authorized |
| `A4P_SERIALIZE_FAIL` | 5001 | Serialization failed |
| `A4P_DESERIALIZE_FAIL` | 5002 | Deserialization failed |
| `A4P_INTERNAL_ERROR` | 6001 | Unexpected internal error |

## 输入与状态说明

- IntentIssueContext 是签发请求的身份输入，不是激活后的执行 Context。SigningEnv 包含设备与应用信息。应用可构造这些输入结构。
- 所有协议数值须为整数，并位于 ±9007199254740991 安全范围内；时间戳非负，数量和金额必须满足对应校验。币种应一致。anchorTime、periodSeconds 和 maxExecutionCountPerPeriod 必须一起提供或一起省略，periodSeconds 单位为秒，最大值为 9007199254740。anchorTime 必须不晚于 validFrom；创建或校验意图时若不满足此约束，返回 A4P_INVALID_PARAMETER。
- ACTIVE 接受新预留；REVOKED 和 EXPIRED 禁止新预留但允许终结已有 PENDING 执行；CONSUMED 表示全局额度耗尽且账本无待处理执行。COMPLETED 与 CANCELED 是执行记录的终态，均不得再次终结；取消不删除账本记录。
- executionInfo 是最近一次相关调用求值时的额度快照，不随读取自动刷新。跨周期完成不会修改旧执行的 periodIndex，也不会释放已完成订单占用的总金额或总执行次数。
