# A4P Java SDK

[English](./README.md) | 简体中文

A4P 支付意图协议的 Java 17 实现，移植自 [PR #130](https://github.com/openJiuwen-ai/agent-protocol/pull/130) 中的 ArkTS SDK。

## 构建与测试

环境要求：JDK 17 及以上、Maven 3.9 及以上。

```sh
cd A4P-java-sdk
mvn test
mvn package
```

### 编译脚本

脚本需要 Bash 环境（例如 Linux、WSL 或 Windows 上的 Git Bash）。在仓库根目录执行：

```sh
bash A4P-java-sdk/scripts/build.sh          # 默认使用 build 模式
bash A4P-java-sdk/scripts/build.sh build    # 显式指定编译打包
bash A4P-java-sdk/scripts/build.sh ut       # 仅运行单元测试
bash A4P-java-sdk/scripts/build.sh --help
```

`build` 使用 `-Dmaven.test.skip=true` 执行 `clean package`，跳过测试代码的编译与执行，
产物为 `A4P-java-sdk/target/a4p-java-sdk-0.1.0.jar`。`ut` 执行 `clean test`，
要求存在测试，测试失败时返回失败，不打包 JAR；Surefire 测试报告位于
`A4P-java-sdk/target/surefire-reports/`。两种模式都会先清理 `target/`，并返回 Maven 的退出码。

额外的 Maven 参数放在模式之后，例如：

```sh
bash A4P-java-sdk/scripts/build.sh build -o
bash A4P-java-sdk/scripts/build.sh ut -Dtest=Rsa3072Test
```

`-o` 表示 Maven 离线模式，所需依赖和插件必须已缓存。脚本根据自身位置定位 `pom.xml`，
不依赖调用时的工作目录。

### 生成 CI 报告的 UT 脚本

如需运行单元测试并生成 CI 报告，在仓库根目录执行以下命令。脚本根据自身位置定位仓库
根目录，不依赖调用者的工作目录；也可通过 `WORKSPACE` 指定仓库根目录的绝对路径来覆盖默认值。

```sh
bash A4P-java-sdk/scripts/ut.sh
```

该脚本需要 Git、Maven 和常用 Shell 工具，其中 `grep` 需要支持 `-P`（例如 GNU grep）。
脚本执行 `mvn clean test`，并返回 Maven 的退出码（`0` 为 `PASS`，非 `0` 为 `FAILED`）。
报告输出到定位到的仓库根目录下的 `outputs/`（设置覆盖值时为 `$WORKSPACE/outputs/`）：

| 文件 | 内容 |
| --- | --- |
| `ut.log` | Maven 测试日志 |
| `unit_test_report.html` | 测试数量统计及最后 200 行日志 |
| `junit.xml` | 合并后的 JUnit XML，仅在存在 Surefire XML 报告时生成 |
| `ut.json` | 状态、Maven 退出码及报告链接 |

可选环境变量 `PR_ID`（默认 `0`）与 `OBS_BUCKET`（默认 `openjiuwen-ci`）用于配置
`ut.json` 中的报告链接。脚本只生成本地报告，上传到链接对应的 OBS 位置需由 CI 另行完成。

构件坐标为 `io.openjiuwen:a4p-java-sdk:0.1.0`。JSON 由 Jackson 处理；JUnit 5 仅用于测试。不需要任何 Web 框架。

`jackson-databind`（此处为 2.20.1）是唯一的运行时依赖，JUnit 5 为 `test` 作用域。

```xml
<dependency>
  <groupId>io.openjiuwen</groupId>
  <artifactId>a4p-java-sdk</artifactId>
  <version>0.1.0</version>
</dependency>
```

可在 IDE 中运行 `io.openjiuwen.a4p.examples.PaymentExample`，或执行：

```sh
mvn compile org.codehaus.mojo:exec-maven-plugin:3.6.3:java "-Dexec.mainClass=io.openjiuwen.a4p.examples.PaymentExample"
```

`pom.xml` 中并未声明 exec 插件，因此该命令首次执行时会从 Maven 中央仓库解析该插件，需要有网络访问。

示例使用相互独立的 Agent、意图中心（intent center）与用户 RSA-3072 密钥对，演示了创建、签发、授权、激活、验签、预留与完成一笔支付。

## API

| 规范 | Java |
| --- | --- |
| `createIntent` | `A4p.createIntent(CreateIntentRequest, Signer[, long now])` 返回 `A4pResult<IntentL1>` |
| `issueIntent` | `A4p.issueIntent(IntentL1, SignatureVerifier, IntentIssueContext, Signer[, long now])` 返回 `A4pResult<IntentL2>` |
| `signIntent` | `A4p.signIntent(IntentL2, SignatureVerifier, SigningEnv, Signer[, long now])` 返回 `A4pResult<IntentL3>` |
| `activateIntent` | `A4p.activateIntent(IntentL3, SignatureVerifier[, long now])` 返回 `A4pResult<IntentExecutionContext>` |
| `verifyIntentAuthenticity` | `A4p.verifyIntentAuthenticity(IntentL3, SignatureVerifier)` 返回 `A4pResult<VerificationResult>` |
| `revokeIntent` | `A4p.revokeIntent(IntentExecutionContext)` 返回 `A4pResult<IntentExecutionContext>` |
| `createExecution / completeExecution / cancelExecution` | `A4p` 上的静态方法，返回 `A4pResult<IntentExecutionContext>` |
| `A4pResult<T>` | `A4pResult`：`success()`、`data()`、`code()`（`long`）、`message()` |
| `A4pException` | `A4pException`，仅由返回值型辅助方法以及 SDK 缺陷抛出 |
| `A4pErrorCode` | `code()`、`message()`，以及把结果码还原为常量的 `fromCode(int)` |
| `A4pIntentCodec` | `A4pIntentCodec.standard()`；`serializeIntentL1/L2/L3` 与 `serializeIntentExecutionContext` 返回 `String`，对应的每个 `deserialize…` 返回 `A4pResult` |
| `Signer / SignatureVerifier` | 函数式接口；`Signer.sign(byte[])` 返回 `SignatureInfo` |
| `Rsa3072` | 可选适配器：`Rsa3072.signer(String, PrivateKey)` 与 `Rsa3072.verifier(Map<String, PublicKey>)` |
| `CanonicalJson` | 签名时使用的规范化辅助方法：`stringify`、`toSignableBytes`、`encodeUtf8`/`decodeUtf8`、`base64UrlEncode`/`base64UrlDecode`、`MAX_SAFE_INTEGER` |
| 类型联合（`StructuredIntent`、`Execution`、`executionInfo`） | `JsonNode` 载荷；`PaymentProfile` 在 AI_PAY 载荷与记录之间相互转换 |
| 数据模型 | `Models` 中的不可变 record 与枚举 |

`[long now]` 表示下文中说明的显式时钟重载。

这些 record 与枚举嵌套在 `Models` 中；`A4p`、`A4pResult`、`A4pException`、`A4pErrorCode`、
`A4pIntentCodec`、`IntentExecutionContext`、`PaymentProfile` 与 `Rsa3072` 是顶层类，因此两个 import 都需要：

```java
import io.openjiuwen.a4p.*;
import io.openjiuwen.a4p.Models.*;
```

AI_PAY 载荷请通过 `PaymentProfile` 构造。下面的片段展示调用形态；完整可编译的版本见
`io.openjiuwen.a4p.examples.PaymentExample`，其中还包含密钥对、L1/L2/L3 链以及规范化序列化的搭建过程。

```java
// 激活一个由 Agent 创建、意图中心签发并签名的意图。
SignatureVerifier userVerifier = Rsa3072.verifier(Map.of("user", userPublicKey));
A4pResult<IntentExecutionContext> activated = A4p.activateIntent(authorizedIntent, userVerifier, now);
if (!activated.success()) { /* 依据 activated.code() 分支处理 */ }
IntentExecutionContext context = activated.data();

// 针对已激活的意图预留一笔订单，然后完成它。
Order order = new Order("order-1", 3000L, "CNY", List.of(new CartItem(product, 1L)), now);
Execution execution = PaymentProfile.execution("execution-1", "1.0", order);
A4pResult<IntentExecutionContext> reserved = A4p.createExecution(context, execution, now);
if (!reserved.success()) { /* 依据 reserved.code() 分支处理，然后持久化 context */ }
A4pResult<IntentExecutionContext> completed = A4p.completeExecution(context, execution.id(), now);

PaymentExecutionInfo info = PaymentProfile.executionInfo(context);
```

失败的 `createExecution` 仍可能已把 context 置为 `EXPIRED`，因此每次调用后都要持久化 context。
`A4pIntentCodec` 上的 `serializeIntentExecutionContext` / `deserializeIntentExecutionContext`
提供持久化形式；反序列化会重新校验存储的 L3（不重新验证用户签名），检查账本是否符合已授权约束，
并要求 `executionInfo` 与账本一致。它还要求状态可达：每个 `reservedAt` 落在 `[validFrom, validUntil]` 内，
`completedAt`/`canceledAt` 不早于 `reservedAt`，并且当且仅当没有待处理记录且全局配额已用尽时状态为
`CONSUMED`（`REVOKED` 与 `EXPIRED` 接受任意账本）。

可选字段使用 `null`；缺席的可选字段不会出现在签名 JSON 中。AI_PAY 载荷内显式的 `null` 会被拒绝。
金额是应用约定的最小货币单位下的正整数。所有协议数值都是限制在 JavaScript 安全整数范围内的整数；
不是整数的值必须以字符串承载。

生命周期与执行方法都同时提供系统时钟重载与显式的 `long now` 重载（epoch 毫秒）。显式 `now`
必须是非负安全整数，并且在其他校验之前最先被验证。生产代码必须提供可信时钟，绝不能使用客户端
提供的时间戳。订单时间戳不决定配额周期，也不决定授权有效期。

必填字符串不能为空白（使用 JavaScript `trim()` 的空白字符集）。一个商品需要非空白的 seller ID，
并且 `spuId`/`skuId` 至少提供一个。周期规则要求 `anchorTime <= validFrom` 且
`periodSeconds <= 9007199254740`。链上时间戳单调：`L1.timestamp <= L2.timestamp <= L3.timestamp`。
激活要求 `L3.timestamp <= now <= validUntil`（`A4P_INVALID_PARAMETER` / `A4P_TIMESTAMP_EXPIRED`）；
尚未生效的意图可以被激活，而在 `validFrom` 之前的预留会以 `A4P_TIMESTAMP_TOO_EARLY` 失败。
`intentConversation` 的长度限制与敏感数据过滤由 Agent 负责。

### 结果码

调用方依据 `code()` 分支处理；`message()` 是固定的诊断文本，取值来自与 ArkTS SDK 共用的
`A4pErrorCode`。`0` 表示成功，其他任何值都是协议失败。`A4pErrorCode.fromCode(int)` 把码还原为常量，
未知值映射为 `A4P_INTERNAL_ERROR`（调用时需把 `code()` 返回的 `long` 转成 `int`）。

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
| 4003 | `A4P_PERIOD_COUNT_EXCEEDED` | 为兼容 ArkTS 错误码而保留；Java 当前不返回此码（周期执行次数超限使用 4004） |
| 4004 | `A4P_PERIOD_EXECUTION_COUNT_EXCEEDED` | 超出周期执行次数上限 |
| 4005 | `A4P_PRODUCT_QUANTITY_EXCEEDED` | 超出商品数量上限 |
| 4006 | `A4P_SINGLE_ORDER_AMOUNT_EXCEEDED` | 超出单笔订单金额上限 |
| 4007 | `A4P_PRODUCT_NOT_AUTHORIZED` | 商品未被授权 |
| 5001 | `A4P_SERIALIZE_FAIL` | 序列化失败 |
| 5002 | `A4P_DESERIALIZE_FAIL` | 反序列化失败 |
| 6001 | `A4P_INTERNAL_ERROR` | 意外的内部错误 |

`2001` 与 `2002` 也覆盖可选适配器：`Rsa3072` 对签名被拒返回 `2002`，抛出异常的
`Signer`/`SignatureVerifier` 会被 SDK 映射为同样的码，详见下文。

## 执行语义

- 待处理记录与已完成记录都会占用金额、次数和白名单数量。
- 显式取消会释放配额；下游结果未知则不会释放。
- 订单购物车必须与白名单分组一一匹配：`cartItems` 与 `itemsWhiteList` 长度相同，并且每个购物车
  条目与一个不同的、列出该商品的分组配对。购物车顺序无关紧要，只要存在一种有效配对即可。
- `orderId` 是支付的幂等键：账本中已存在的订单 ID（含已取消记录）会以 `A4P_DUPLICATE_ID` 被拒绝。
- 重复的执行 ID 在取消之后依然会被拒绝。调用方提供的执行 ID 不能为空白；自动生成的 ID 会写入账本记录。
- 预留检查按固定顺序执行：请求结构（包括提供的执行 ID 非空白）；版本、动作与币种是否与已授权意图
  一致；context 状态；有效期窗口（惰性置为 EXPIRED）；执行/订单 ID 重复；随后是执行次数上限、
  单笔订单金额上限、剩余金额、购物车一一匹配、白名单数量，最后是周期配额。
- 购物车长度不匹配会以 `A4P_INVALID_PARAMETER` 失败；购物车不存在有效配对（包括商品不在任何分组中）
  会以 `A4P_PRODUCT_NOT_AUTHORIZED` 失败。在数量校验中，一个条目仍会针对每一个列出其商品的分组计数。
- 撤销会阻止预留，但允许对待处理记录执行完成/取消。
- 超过 `validUntil` 的预留会把 context 标记为 EXPIRED，尽管该次调用失败。待处理记录在 EXPIRED 状态下
  仍可被完成或取消。
- 创建执行、完成与取消操作若 `now` 早于上下文的 `activatedTime`，会以 `A4P_TIMESTAMP_TOO_EARLY`（3003）被拒绝。
- 当 `activatedTime <= now < reservedAt` 时，完成与取消操作会以 `A4P_INVALID_PARAMETER`（1001）被拒绝；记录保持待处理。
- 当一次完成操作之后不再有待处理记录，且要么达到次数上限、要么剩余金额为 0 时，ACTIVE 意图变为
  CONSUMED。REVOKED 与 EXPIRED 是终态。
- `executionInfo()` 始终给出 `leftExecutionCount`、`leftAmount`、`periodIndex` 与
  `leftPeriodExecutionCount`；不适用的数值为 `null`。
- context 操作对校验与更新加锁。这保护共享同一个 context 的线程，而**不**保护多个进程或各自独立激活
  的副本。
- `A4pIntentCodec.standard()` 返回无状态的共享单例，因此一个实例可以服务所有线程。

账本位于内存中。意图中心必须在以 `intentId` 加锁的数据库事务中持久化 context，防止同一个 `intentId`
被重放，并把密钥与身份绑定。激活会校验最外层的用户签名；`verifyIntentAuthenticity` 校验 L3 链与意图
中心的 L2 签名，不校验用户签名，也不依赖 context 状态。

## 线格式与安全

参见 [canonical JSON 规范](docs/canonical-json-spec.md)。JSON 字段名遵循规范
（`intentConversation`、`resources`、`UserIdentity.id`），每个枚举都按名称序列化
（`"AI_PAY"`、`"CONSUMED"`、`"PENDING"`），协议版本为 `1.0`。在规范确定它们之前，HTTP 绑定不属于本 SDK。

每一层的签名覆盖该层除去整个 `sign` 对象之后的内容，因此该层自己的 `alg` 与 `keyIdentifier`
不在其自身签名字节内；它们由下一层的签名覆盖。这是规范规定的设计。`alg` 必须是 `RSA3072`。
验证方必须只从可信注册表中解析密钥，并把每个密钥绑定到允许的算法；绝不要信任传入的 `alg`。

`Rsa3072` 是一个可选但可用的适配器，使用 JCA `SHA256withRSA`（PKCS#1 v1.5）、3072 位 RSA 密钥
和无填充的 Base64URL。其 `VerificationResult.code` 成功为 `0`、失败为 `2002`，失败原因放在
`message` 中。缺失信任映射表、空白密钥标识符或缺失/非 3072 位密钥时，其工厂方法抛出
`A4pException(A4P_INVALID_PARAMETER)`。对端必须就该套件达成一致，或提供相匹配的
`Signer`/`SignatureVerifier` 实现。任何验证器返回的 `VerificationResult` 都必须在 `code` 为 `0`
时恰好令 `valid` 为 true；结果不一致时按验证失败处理。

意图记录与嵌套载荷不可变；`JsonNode` 载荷在进出时都会被复制。该 Java 移植还拒绝重复的 JSON 键、
未知的模型字段、非法 UTF-8、带小数的整数字段、数值型枚举值、标量强制转换以及非法的回调结果，
而不是静默地规范化安全敏感输入。

## 版本与兼容性

协议版本是 `1.0`（`A4p.PROTOCOL_VERSION`）。本 SDK 创建的每一层都写入该常量，每一步都会校验所收到的
各层，不一致时以 `A4P_UNSUPPORTED_VERSION` 拒绝。该移植与它所来自的 ArkTS SDK（`A4P-arkts-sdk/`）
实现相同的线格式，canonical-vector 测试让两者保持一致，因此在双方都使用 canonical JSON 规则与同一
签名套件的前提下，意图与持久化的 context 可以在两个 SDK 之间互换。构件版本为 `0.1.0`；
协议兼容性由 `version` 字段决定，而不是由构件版本决定。

## 已知限制

- 账本是内存内的、以单个 context 为粒度。持久化、加锁写入、防重放以及密钥与身份的绑定都由意图中心负责。
- 同步只覆盖共享同一个 `IntentExecutionContext` 的线程，不覆盖多个进程，也不覆盖同一个 `intentId`
  各自独立激活的副本。
- 在规范确定之前，HTTP 绑定不在范围内；仅提供进程内调用与 canonical JSON 序列化。
- `Rsa3072` 是约定好的一个套件，而非协商结果；对端要么使用它，要么提供相匹配的
  `Signer`/`SignatureVerifier` 实现。
- `verifyIntentAuthenticity` 有意不校验用户签名。
- 该移植在若干处比协议最低要求更严格（重复 JSON 键、未知字段、数值枚举、标量强制转换），以确保安全
  敏感输入永远不会被静默规范化。下文的对齐文档记录了改了什么以及为什么。

## 更多文档

参考文档位于 `docs/` 目录：

| 文档 | 内容 |
| --- | --- |
| [canonical-json-spec.md](docs/canonical-json-spec.md) | 每次签名背后的 canonical JSON 与 Base64URL 规则。 |

## 测试

测试覆盖生命周期、配额回滚、订单幂等、购物车一一匹配、检查顺序、过期终结、终态、消费、白名单校验、
周期边界、空白与结构校验、结果模型、类型联合载荷、线格式名称与枚举编码、并发预留、canonical 向量、
分离的签名元数据、畸形输入、验证器结果一致性、执行 context 的持久化与恢复（包括状态可达性与记录
时间戳），以及真实的 RSA 签名验证与篡改检测。测试夹具中的 digest 签名器只是一个确定性的测试替身，
不得用于授权。
