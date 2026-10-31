# Serialization and Signature Rules

A4P uses JSON as the serialization format for Intents. To ensure that implementations in different languages, such as Java and ArkTS, generate identical signature inputs, the object to be signed must be serialized deterministically before signing or signature verification.

## JSON Canonicalization

A4P uses the [RFC 8785](https://www.rfc-editor.org/info/rfc8785/) JSON Canonicalization Scheme (JCS) for JSON canonicalization. Signers and verifiers must generate the bytes to be signed using the same canonicalization rules.

JCS canonicalization meets the following requirements:

* JSON object fields are sorted deterministically according to JCS rules, with nested objects processed recursively;
* JSON arrays retain their original element order; canonicalization must not reorder array elements;
* Whitespace that does not affect semantics is omitted;
* String contents retain their original Unicode data, without additional Unicode normalization;
* JSON objects must not contain duplicate field names;
* The canonicalized JSON is encoded as UTF-8 to produce the `byte[]` to be signed.

Note: JSON in HTTP messages does not need to be canonicalized and can be transmitted directly. The receiver should first parse the JSON, then regenerate canonicalized bytes according to the rules above for signature verification. Therefore, field order and formatting whitespace in HTTP messages do not affect signature verification.

Examples:

```text
{"b":1,"a":2}                 -> {"a":2,"b":1}
[3,1,2]                       -> [3,1,2]
{"10":1,"2":2}                -> {"10":1,"2":2}
9007199254740991               -> 9007199254740991
{"sign":"outer","nested":{"sign":"inner"}}
signed bytes                  -> {"nested":{"sign":"inner"}}
```

See the `CanonicalJson` class for the Java SDK implementation.

## Serialization/Deserialization API

This API converts Intent protocol objects to and from their standard serialized representations. Signing requires serialization/deserialization through the interfaces provided by the following class.

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

All serialization APIs accept protocol objects that have already been constructed within the SDK.

Deserialization APIs handle external input and therefore consistently return `A4pResult<T>` to represent errors such as invalid formats and unsupported versions.

## Signature Coverage at Each Layer

The `sign` field in each of the three Intent layers is a `SignatureInfo` object (containing `alg`, `keyIdentifier`, and `sign`). Each layer's signature covers the entire object at that layer except its own `sign` field (the entire `SignatureInfo` object). Fields at that layer, such as `timestamp`, are included in the signed content.

The `alg` and `keyIdentifier` fields in a layer's own `SignatureInfo` are excluded from that layer's signed content. This is intentional: they are covered by the next signature—the complete `SignatureInfo` objects of L1 and L2 are included in the signature inputs of L2 and L3, respectively. The outermost L3 `alg` and `keyIdentifier` fields are not protected by a signature. The verifier must retrieve the key from a trusted registry using only `keyIdentifier` and use the algorithm bound to that key; it must not trust the `alg` value in the message.

```text
L1 signature input = Canonicalize(IntentL1 excluding L1.sign)

L2 signature input = Canonicalize(IntentL2 excluding L2.sign)
                     Includes the complete IntentL1, including L1.sign

L3 signature input = Canonicalize(IntentL3 excluding L3.sign)
                     Includes the complete IntentL2, including L1.sign and L2.sign
```

Example of signature coverage for a single layer:

```json
{"sign":"outer","nested":{"sign":"inner"}}
signed bytes                  -> {"nested":{"sign":"inner"}}
```

## Signing and Signature Verification

The recommended asymmetric cryptographic algorithm for `alg` is **RSA3072**.

The signing process is as follows:

```text
Intent (sign is empty)
    ↓
Remove the current layer's sign field (the entire SignatureInfo)
    ↓
JCS Canonicalize
    ↓
UTF-8 bytes
    ↓
Signer.sign(bytes)
    ↓
Write the returned SignatureInfo (alg / keyIdentifier / sign) to the current layer's sign field
```

Note 1: `Signer.sign()` returns a `SignatureInfo` object whose `sign` field is the signature bytes **encoded as Base64URL without padding**. The SDK validates the returned value (`alg` must be a defined algorithm, and `keyIdentifier` and `sign` must be non-blank). An invalid value is treated as a signing failure.

The signature verification process is as follows:

```text
Intent (with sign)
    ↓
Read the current layer's sign (SignatureInfo)
    ↓
Remove the current layer's sign field
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
Return VerificationResult
```

Note 2: `SignatureVerifier` is responsible for resolving a trusted key using `keyIdentifier`, checking that `alg` matches the key type, and decoding `sign` from Base64URL without padding before verifying the signature.

### RSA3072: L1 Signing and Verification Example

The following program uses [`Rsa3072.java`](../examples/src/main/java/io/openjiuwen/a4p/examples/Rsa3072.java) from the standalone examples module to create an Agent signer and verifier. `A4p.createIntent` generates the canonicalized L1 bytes to be signed, calls the Agent's `Signer`, and writes the returned signature to `L1.sign`. `A4p.issueIntent` calls the Agent's `SignatureVerifier` to verify this signature and generates L2 only after verification succeeds. Callers do not need to serialize L1 or remove the `sign` field manually.

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
                "Buy two books", "Book purchase authorization",
                PaymentProfile.structuredIntent(
                        new ShoppingItems(List.of(new ItemWhiteList(List.of(product), 2L))),
                        constraints),
                null);

        // The SDK canonicalizes L1, calls agentSigner, and populates L1.sign.
        IntentL1 l1 = requireSuccess(A4p.createIntent(request, agentSigner, now));
        System.out.println("L1 signed, key ID: " + l1.sign().keyIdentifier());

        IntentIssueContext identities = new IntentIssueContext(
                new UserIdentity("example", "user-1", null),
                new AgentIdentity("example", "agent-1", null),
                new ServiceProviderIdentity("shop", null));
        // The SDK verifies L1.sign; on success, centerSigner issues L2.
        IntentL2 l2 = requireSuccess(A4p.issueIntent(l1, agentVerifier, identities, centerSigner, now));
        System.out.println("L1 signature verified, L2 generated: " + l2.version());
    }

    private static <T> T requireSuccess(A4pResult<T> result) {
        if (!result.success()) {
            throw new IllegalStateException(result.code() + " " + result.message());
        }
        return result.data();
    }
}
```

`keyIdentifier` must map to the correct public key in the verifier's trusted public key registry. The two key pairs in the example are for demonstration only; in production, each party manages its own keys.
