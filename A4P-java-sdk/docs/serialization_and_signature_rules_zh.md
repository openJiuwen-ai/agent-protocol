# 序列化与签名规则

A4P 使用 JSON 作为 Intent 的序列化格式。为保证 Java、ArkTS 等不同语言实现能够生成一致的签名输入，签名和验签前必须对待签名对象进行确定性序列化。

## JSON 规范化

A4P 的 JSON 规范化采用 [RFC 8785](https://www.rfc-editor.org/info/rfc8785/) JSON Canonicalization Scheme（JCS）。签名方和验签方必须基于同一规范化规则生成待签名字节。

JCS 规范化处理满足以下要求：

* JSON Object 的字段按照 JCS 规则进行确定性排序，并递归处理嵌套 Object；

* JSON Array 保持元素原有顺序，禁止因规范化改变数组元素顺序；

* 不输出对语义无影响的空白字符；

* 字符串内容保持原始 Unicode 数据，不额外进行 Unicode Normalization；

* JSON 对象中不得包含重复字段名；

* 规范化后的 JSON 使用 UTF\-8 编码转换为待签名 `byte[]`；

注：HTTP 报文中的 JSON 无需规范化，可直接传输。接收方应先解析 JSON，再按照上述规则重新生成规范化字节用于验签，因此 HTTP 报文的字段顺序和格式化空白不影响签名验证。

样例:

```text
{"b":1,"a":2}                 -> {"a":2,"b":1}
[3,1,2]                       -> [3,1,2]
{"10":1,"2":2}                -> {"10":1,"2":2}
9007199254740991               -> 9007199254740991
{"sign":"outer","nested":{"sign":"inner"}}
signed bytes                  -> {"nested":{"sign":"inner"}}
```

Java SDK 的实现查看 CanonicalJson 类

## 序列化/反序列化 API

负责 Intent 协议对象与标准序列化表示之间的转换。签名时需要利用下面的类提供的接口进行序列化/反序列化

```java
public class A4pIntentCodec {

    String serializeIntentL1(
            IntentL1 intentL1
    );

    A4pResult<IntentL1> deserializeIntentL1(
            String serialized
    );

    String serializeIntentL2(
            IntentL2 intentL2
    );

    A4pResult<IntentL2> deserializeIntentL2(
            String serialized
    );

    String serializeIntentL3(
            IntentL3 intentL3
    );

    A4pResult<IntentL3> deserializeIntentL3(
            String serialized
    );

    String serializeIntentExecutionContext(
            IntentExecutionContext intentExecutionContext
    );

    A4pResult<IntentExecutionContext> deserializeIntentExecutionContext(
            String serialized
    );
}
```

序列化 API 的输入均为 SDK 内已经构造完成的协议对象。

反序列化 API 面向外部输入，因此统一返回 `A4pResult<T>`，用于表达非法格式、版本不支持等错误。

## 各层签名覆盖范围

三层 Intent 的 `sign` 字段均为 `SignatureInfo`（含 `alg`、`keyIdentifier`、`sign`）。每层签名覆盖该层除自身 `sign` 字段（整个 `SignatureInfo` 对象）以外的完整对象，`timestamp` 等本层字段均属于签名内容。

本层 `SignatureInfo` 中的 `alg`、`keyIdentifier` 不属于本层签名内容，这是有意的设计：它们由下一步的签名覆盖——L1、L2 的 `SignatureInfo` 分别完整包含在 L2、L3 的签名输入中；最外层 L3 的 `alg`、`keyIdentifier` 不受签名保护，验签方必须只按 `keyIdentifier` 从可信注册表获取密钥，并使用与该密钥绑定的算法，不得信任报文中的 `alg`。

```text
L1 签名输入 = Canonicalize(IntentL1 excluding L1.sign)

L2 签名输入 = Canonicalize(IntentL2 excluding L2.sign)
              其中包含完整 IntentL1，包括 L1.sign

L3 签名输入 = Canonicalize(IntentL3 excluding L3.sign)
              其中包含完整 IntentL2，包括 L1.sign 和 L2.sign
```

一层的签名范围样例如下:

```json
{"sign":"outer","nested":{"sign":"inner"}}
signed bytes                  -> {"nested":{"sign":"inner"}}
```

## 签名与验签流程

`alg`推荐采用 **RSA3072** 非对称加密算法。

签名流程如下：

```text
Intent（sign 为空）
    ↓
移除当前层 sign 字段（整个 SignatureInfo）
    ↓
JCS Canonicalize
    ↓
UTF-8 bytes
    ↓
Signer.sign(bytes)
    ↓
返回的 SignatureInfo（alg / keyIdentifier / sign）写入当前层 sign
```

注1：`Signer.sign()` 返回 `SignatureInfo`，其中 `sign` 是签名字节经 **Base64URL 编码（不含 padding）** 得到的字符串。SDK 会校验返回值（`alg` 为已定义的算法、`keyIdentifier` 与 `sign` 非空白），不合法时按签名失败处理。

验签流程如下：

```text
Intent（包含 sign）
    ↓
读取当前层 sign（SignatureInfo）
    ↓
移除当前层 sign 字段
    ↓
JCS Canonicalize
    ↓
UTF-8 bytes
    ↓
SignatureVerifier.verify(
    bytes,
    signatureInfo
)
    ↓
输出 VerificationResult
```

注2：`SignatureVerifier` 负责按 `keyIdentifier` 解析可信密钥、校验 `alg` 与密钥类型匹配，并将 `sign` 按 Base64URL（不含 padding）解码后验签。

### RSA3072：L1 签名与验签样例

下面的程序使用独立示例模块中的 [`Rsa3072.java`](../examples/src/main/java/io/openjiuwen/a4p/examples/Rsa3072.java) 创建 Agent 签名器和验签器。`A4p.createIntent` 会生成 L1 的规范化待签名字节、调用 Agent 的 `Signer`，并把返回的签名写入 `L1.sign`；`A4p.issueIntent` 会调用 Agent 的 `SignatureVerifier` 验证这个签名，验证通过后才生成 L2。调用方无需手动序列化 L1 或移除 `sign` 字段。

```java
package io.openjiuwen.a4p.examples;

import io.openjiuwen.a4p.A4p;
import io.openjiuwen.a4p.A4pResult;
import io.openjiuwen.a4p.PaymentProfile;
import io.openjiuwen.a4p.Signer;
import io.openjiuwen.a4p.SignatureVerifier;
import io.openjiuwen.a4p.Models.*;
import java.security.KeyPairGenerator;
import java.util.List;
import java.util.Map;

public final class Rsa3072SignVerifyExample {
    public static void main(String[] args) throws Exception {
        KeyPairGenerator generator = KeyPairGenerator.getInstance("RSA");
        generator.initialize(3072);
        var agentKeys = generator.generateKeyPair();
        var centerKeys = generator.generateKeyPair();

        Signer agentSigner = Rsa3072.signer("agent-key-1", agentKeys.getPrivate());
        SignatureVerifier agentVerifier = Rsa3072.verifier(
                Map.of("agent-key-1", agentKeys.getPublic()));
        Signer centerSigner = Rsa3072.signer("center-key-1", centerKeys.getPrivate());

        long now = System.currentTimeMillis();
        Product product = new Product(new SellerInfo("shop"), new SpuSkuInfo("book", "paperback"));
        PaymentConstraints constraints = new PaymentConstraints(
                5000L, 10000L, "CNY", now, now + 60_000L, 2L, null, null, null);
        CreateIntentRequest request = new CreateIntentRequest(
                "购买两本书", "购买书籍授权",
                PaymentProfile.structuredIntent(
                        new ShoppingItems(List.of(new ItemWhiteList(List.of(product), 2L))),
                        constraints),
                null);

        // SDK 规范化 L1、调用 agentSigner，并填充 L1.sign。
        IntentL1 l1 = requireSuccess(A4p.createIntent(request, agentSigner, now));
        System.out.println("L1 已签名，密钥 ID: " + l1.sign().keyIdentifier());

        IntentIssueContext identities = new IntentIssueContext(
                new UserIdentity("example", "user-1", null),
                new AgentIdentity("example", "agent-1", null),
                new ServiceProviderIdentity("shop", null));
        // SDK 验证 L1.sign；成功后由 centerSigner 签发 L2。
        IntentL2 l2 = requireSuccess(A4p.issueIntent(l1, agentVerifier, identities, centerSigner, now));
        System.out.println("L1 签名验证通过，已生成 L2: " + l2.version());
    }

    private static <T> T requireSuccess(A4pResult<T> result) {
        if (!result.success()) {
            throw new IllegalStateException(result.code() + " " + result.message());
        }
        return result.data();
    }
}
```

`keyIdentifier` 必须在验签方可信公钥表中对应到正确公钥。上例的两个密钥对仅用于演示，实际业务由各方管理自己的密钥。
