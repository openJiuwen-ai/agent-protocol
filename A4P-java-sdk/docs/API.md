# Java SDK public API

[English](API.md) | [简体中文](API_zh.md)

This reference describes the current public API in `io.openjiuwen.a4p`, targeting Java 17 and A4P Core `1.0` (`A4p.PROTOCOL_VERSION`). The supported action type is `AI_PAY`.

## Imports and lifecycle

```java
import io.openjiuwen.a4p.*;
import io.openjiuwen.a4p.Models.*;
```

Protocol records and enums are nested in `Models`; importing `A4p` alone does not import them. `A4p` provides static lifecycle operations, `PaymentProfile` constructs and decodes payment payloads, and `new A4pIntentCodec()` provides canonical JSON serialization and restoration.

```text
createIntent → issueIntent → signIntent → activateIntent
                                             ↓
                                      createExecution
                                             ↓
                             completeExecution / cancelExecution
```

Create and restore layered intents and execution contexts through SDK APIs. Applications may construct request, identity, environment and payment input records. Do not repeat the same operation for the same Intent or Execution; lifecycle operations must be called in order. One Context can process multiple distinct Executions. Queries and serialization can be repeated.

The SDK reserves and accounts for authorization; the application performs the external payment and then completes or cancels its reservation. Persist the activated context and every subsequent update. Serialize each intent's database read-update-write cycle in a transaction; synchronization on one in-memory context does not coordinate multiple restored copies or processes. A failed reservation can set the supplied context to EXPIRED: persist that context even though the failure result has no data.

## Results and exceptions

Lifecycle methods and deserializers return `A4pResult<T>`. Check `success()` before reading `data()`: success has non-null data and code `0`; failure has null data and a nonzero code. Branch on numeric codes rather than message text. The public result constructor throws `IllegalArgumentException` for inconsistent success/data/code combinations. `toString()` excludes business payloads.

Serialization, payment helpers and model normalization can throw `A4pException`; inspect `code()` for its `A4pErrorCode`. Unexpected runtime defects in result-returning operations surface as `A4pException(A4P_INTERNAL_ERROR)` rather than ordinary failure results.

`verifyIntentAuthenticity` has two result levels: `result.success()` means the SDK call succeeded, while `result.data().valid()` is the issuer-verification verdict. A consistent negative verdict can be returned inside a successful SDK result.

## Timestamp rules

All timestamps and explicit `now` arguments are Unix epoch milliseconds. Overloads without `now` use `System.currentTimeMillis()`; explicit clocks must be trusted. Numeric protocol values are integers within ±9007199254740991; timestamps are non-negative. Amounts and quantities are positive integers. Amount units are application-defined minor currency units.

| Operation | Time requirements |
| --- | --- |
| `issueIntent` | `now >= L1.timestamp`; otherwise `A4P_INVALID_PARAMETER`. |
| `signIntent` | `now >= L2.timestamp` and `now <= validUntil`; otherwise invalid parameter or expired. These checks precede verification and signing. |
| `activateIntent` | `now >= L3.timestamp` and `now <= validUntil`; otherwise invalid parameter or expired. Stores `activatedTime = now`. |
| `createExecution` | `now >= activatedTime` and `validFrom <= now <= validUntil`. Too early returns 3003; expiration returns 3002 and can mark the context EXPIRED. |
| `completeExecution`, `cancelExecution` | `now >= reservedAt`; otherwise 3003. Existing reservations can be finalized after expiry or revocation. |
| `revokeIntent` | `now >= activatedTime`; otherwise 3003. No authorization validity-window restriction. |

Signing and activation before `validFrom` are allowed. Equal timestamps and both validity boundaries are allowed. `Order.timestamp` is validated as a timestamp but does not determine reservation validity or quota periods. Reading context status does not automatically evaluate expiry.

## Function reference

| Type | Signature | Behavior |
| --- | --- | --- |
| `A4p` | `static A4pResult<IntentL1> createIntent(CreateIntentRequest request, Signer agentSigner)` | Create and agent-sign L1; generate intentId when omitted. |
| `A4p` | `static A4pResult<IntentL1> createIntent(CreateIntentRequest request, Signer agentSigner, long now)` | Create and agent-sign L1; generate intentId when omitted. |
| `A4p` | `static A4pResult<IntentL2> issueIntent(IntentL1 intent, SignatureVerifier agentVerifier, IntentIssueContext context, Signer serverSigner)` | Validate L1 and its agent signature, bind identities and server-sign L2. |
| `A4p` | `static A4pResult<IntentL2> issueIntent(IntentL1 intent, SignatureVerifier agentVerifier, IntentIssueContext context, Signer serverSigner, long now)` | Validate L1 and its agent signature, bind identities and server-sign L2. |
| `A4p` | `static A4pResult<IntentL3> signIntent(IntentL2 intent, SignatureVerifier serverVerifier, SigningEnv environment, Signer userSigner)` | Verify the L2 server signature, attach the signing environment and user-sign L3. Checks both L2 and L1 versions. Requires `now >= L2.timestamp` (`A4P_INVALID_PARAMETER` if earlier) and `now <= validUntil` (`A4P_TIMESTAMP_EXPIRED`, 3002, if later) before verification or user signing. Signing before `validFrom` is allowed; `createExecution` rejects early reservations with `A4P_TIMESTAMP_TOO_EARLY` (3003). Uses the system clock for `now`. |
| `A4p` | `static A4pResult<IntentL3> signIntent(IntentL2 intent, SignatureVerifier serverVerifier, SigningEnv environment, Signer userSigner, long now)` | Verify the L2 server signature, attach the signing environment and user-sign L3. Checks both L2 and L1 versions. Requires `now >= L2.timestamp` (`A4P_INVALID_PARAMETER` if earlier) and `now <= validUntil` (`A4P_TIMESTAMP_EXPIRED`, 3002, if later) before verification or user signing. Signing before `validFrom` is allowed; `createExecution` rejects early reservations with `A4P_TIMESTAMP_TOO_EARLY` (3003). |
| `A4p` | `static A4pResult<IntentExecutionContext> activateIntent(IntentL3 intent, SignatureVerifier userVerifier)` | Verify the L3 user signature and create an ACTIVE context with activatedTime. |
| `A4p` | `static A4pResult<IntentExecutionContext> activateIntent(IntentL3 intent, SignatureVerifier userVerifier, long now)` | Verify the L3 user signature and create an ACTIVE context with activatedTime. |
| `A4p` | `static A4pResult<VerificationResult> verifyIntentAuthenticity(IntentL3 intent, SignatureVerifier issuerVerifier)` | Check chain structure, versions and the L2 issuer signature; return the verification verdict without checking the user signature. |
| `A4p` | `static A4pResult<IntentExecutionContext> revokeIntent(IntentExecutionContext context)` | Revoke an ACTIVE context; existing pending executions may still be completed or canceled. |
| `A4p` | `static A4pResult<IntentExecutionContext> revokeIntent(IntentExecutionContext context, long now)` | Revoke an ACTIVE context; existing pending executions may still be completed or canceled. |
| `A4p` | `static A4pResult<IntentExecutionContext> createExecution(IntentExecutionContext context, Execution execution)` | Check validity, status, unique IDs, order and quotas; append a PENDING reservation and refresh accounting. |
| `A4p` | `static A4pResult<IntentExecutionContext> createExecution(IntentExecutionContext context, Execution execution, long now)` | Check validity, status, unique IDs, order and quotas; append a PENDING reservation and refresh accounting. |
| `A4p` | `static A4pResult<IntentExecutionContext> completeExecution(IntentExecutionContext context, String executionId)` | Complete a pending execution and refresh current-period executionInfo; consume an active context when global quota is exhausted and no execution is pending. |
| `A4p` | `static A4pResult<IntentExecutionContext> completeExecution(IntentExecutionContext context, String executionId, long now)` | Complete a pending execution and refresh current-period executionInfo; consume an active context when global quota is exhausted and no execution is pending. |
| `A4p` | `static A4pResult<IntentExecutionContext> cancelExecution(IntentExecutionContext context, String executionId)` | Cancel a pending execution, release its quotas and refresh accounting while retaining the ledger record. |
| `A4p` | `static A4pResult<IntentExecutionContext> cancelExecution(IntentExecutionContext context, String executionId, long now)` | Cancel a pending execution, release its quotas and refresh accounting while retaining the ledger record. |
| `A4pErrorCode` | `int code()` | Read the result code or exception error code. |
| `A4pErrorCode` | `String message()` | Read the diagnostic message (SDK-generated results use fixed English messages). |
| `A4pErrorCode` | `static A4pErrorCode fromCode(int code)` | Map a numeric code to its enum; unknown codes map to internal error. |
| `A4pException` | `A4pException(A4pErrorCode code)` | Construct a runtime exception with a code and optional message and cause. |
| `A4pException` | `A4pException(A4pErrorCode code, String message, Throwable cause)` | Construct a runtime exception with a code and optional message and cause. |
| `A4pException` | `A4pErrorCode code()` | Read the result code or exception error code. |
| `A4pIntentCodec` | `A4pIntentCodec()` | Construct the public, reusable canonical JSON codec. |
| `A4pIntentCodec` | `String serializeIntentL1(IntentL1 intentL1)` | Serialize IntentL1 as canonical JSON. |
| `A4pIntentCodec` | `A4pResult<IntentL1> deserializeIntentL1(String serialized)` | Restore from local JSON with structural checks; this does not replace signature or integrity verification. |
| `A4pIntentCodec` | `String serializeIntentL2(IntentL2 intentL2)` | Serialize IntentL2 as canonical JSON. |
| `A4pIntentCodec` | `A4pResult<IntentL2> deserializeIntentL2(String serialized)` | Restore from local JSON with structural checks; this does not replace signature or integrity verification. |
| `A4pIntentCodec` | `String serializeIntentL3(IntentL3 intentL3)` | Serialize IntentL3 as canonical JSON. |
| `A4pIntentCodec` | `A4pResult<IntentL3> deserializeIntentL3(String serialized)` | Restore from local JSON with structural checks; this does not replace signature or integrity verification. |
| `A4pIntentCodec` | `String serializeIntentExecutionContext(IntentExecutionContext intentExecutionContext)` | Serialize IntentExecutionContext as canonical JSON. |
| `A4pIntentCodec` | `A4pResult<IntentExecutionContext> deserializeIntentExecutionContext(String serialized)` | Restore from local JSON with structural checks; this does not replace signature or integrity verification. |
| `A4pResult` | `boolean success()` | Read the success flag. |
| `A4pResult` | `T data()` | Read success data, or null on failure. |
| `A4pResult` | `long code()` | Read the result code or exception error code. |
| `A4pResult` | `String message()` | Read the diagnostic message (SDK-generated results use fixed English messages). |
| `A4pResult` | `String toString()` | Return a result summary without business payloads. |
| `A4pResult` | `A4pResult(boolean success, T data, long code, String message)` | Construct a result; success requires non-null data and code 0, failure requires null data and a nonzero code. |
| `IntentExecutionContext` | `long activatedTime()` | Read the activation timestamp in epoch milliseconds, preserved across serialization. |
| `IntentExecutionContext` | `String version()` | Read the protocol version. |
| `IntentExecutionContext` | `String intentId()` | Read the associated intent identifier. |
| `IntentExecutionContext` | `IntentL3 authorizedIntent()` | Read the authorized L3 snapshot. |
| `IntentExecutionContext` | `synchronized IntentExecutionStatus status()` | Read the intent execution status. |
| `IntentExecutionContext` | `synchronized List<ExecutionRecord> executions()` | Read an immutable ledger snapshot. |
| `IntentExecutionContext` | `synchronized JsonNode executionInfo()` | Read the accounting snapshot; reads do not advance its period, execution calls refresh it. |
| `PaymentProfile` | `static StructuredIntent structuredIntent(ShoppingItems resources, PaymentConstraints constraints)` | Wrap payment resources and constraints as an AI_PAY structured intent. |
| `PaymentProfile` | `static Execution execution(String id, Order order)` | Wrap an order as an AI_PAY execution request. |
| `PaymentProfile` | `static ShoppingItems resources(StructuredIntent intent)` | Read and decode AI_PAY resources. |
| `PaymentProfile` | `static PaymentConstraints constraints(StructuredIntent intent)` | Read and decode AI_PAY constraints. |
| `PaymentProfile` | `static Order order(Execution execution)` | Read and decode the payment order in an execution. |
| `PaymentProfile` | `static PaymentExecutionInfo executionInfo(IntentExecutionContext context)` | Read the accounting snapshot; reads do not advance its period, execution calls refresh it. |
| `SignatureVerifier` | `VerificationResult verify(byte[] data, SignatureInfo signature) throws Exception` | Verify SDK-provided bytes and signature and return VerificationResult. |
| `Signer` | `SignatureInfo sign(byte[] data) throws Exception` | Sign the SDK-provided bytes and return SignatureInfo. |

## Public data types

All rows below are nested in `io.openjiuwen.a4p.Models`, for example `io.openjiuwen.a4p.Models.IntentL1`. Fields are listed in canonical constructor order; accessors have the component name, such as `order.amount()`.

| Type | Description and fields |
| --- | --- |
| `io.openjiuwen.a4p.Models.SellerInfo` | Seller identifier; `String id` |
| `io.openjiuwen.a4p.Models.SpuSkuInfo` | Product SPU/SKU identifiers; `String spuId, String skuId` |
| `io.openjiuwen.a4p.Models.Product` | Seller and product identity; `SellerInfo seller, SpuSkuInfo spusku` |
| `io.openjiuwen.a4p.Models.CartItem` | Cart product and quantity; `Product product, Long quantity` |
| `io.openjiuwen.a4p.Models.ItemWhiteList` | Allowed products and quantity; `List<Product> whiteList, Long quantity` |
| `io.openjiuwen.a4p.Models.ShoppingItems` | Payment resource whitelist; `List<ItemWhiteList> itemsWhiteList` |
| `io.openjiuwen.a4p.Models.PaymentConstraints` | Amount, currency, validity, execution-count and period limits; `Long singleOrderMaxAmount, Long totalMaxAmount, String currency, Long validFrom, Long validUntil, Long maxExecutionCount, Long anchorTime, Long periodSeconds, Long maxExecutionCountPerPeriod` |
| `io.openjiuwen.a4p.Models.Order` | Order identifier, amount, currency, products and timestamp; `String orderId, Long amount, String currency, List<CartItem> cartItems, Long timestamp` |
| `io.openjiuwen.a4p.Models.PaymentExecutionInfo` | Remaining quotas and snapshot period; `Long leftExecutionCount, long leftAmount, Long periodIndex, Long leftPeriodExecutionCount` |
| `io.openjiuwen.a4p.Models.StructuredIntent` | Action and associated resources and constraints; `IntentActionType action, JsonNode resources, JsonNode constraints` |
| `io.openjiuwen.a4p.Models.UserIdentity` | User identity; `String identityProvider, String id, String additionalInfo` |
| `io.openjiuwen.a4p.Models.AgentIdentity` | Agent identity; `String identityProvider, String id, String additionalInfo` |
| `io.openjiuwen.a4p.Models.ServiceProviderIdentity` | Service provider identity; `String id, String additionalInfo` |
| `io.openjiuwen.a4p.Models.SigningEnv` | Signing device and app environment; `Map<String, Object> device, Map<String, Object> app` |
| `io.openjiuwen.a4p.Models.SignatureInfo` | Algorithm, key identifier and signature; `String alg, String keyIdentifier, String sign` |
| `io.openjiuwen.a4p.Models.CreateIntentRequest` | Input for creating L1; `String intentConversation, String intentSummary, StructuredIntent structuredIntent, String intentId` |
| `io.openjiuwen.a4p.Models.IntentIssueContext` | Identity context input for issuing L2; `UserIdentity userIdentity, AgentIdentity agentIdentity, ServiceProviderIdentity serviceProviderIdentity` |
| `io.openjiuwen.a4p.Models.VerificationResult` | Verification validity, code and message; `boolean valid, long code, String message` |
| `io.openjiuwen.a4p.Models.IntentL1` | Agent-created level-one intent; `String version, String intentId, String intentConversation, String intentSummary, StructuredIntent structuredIntent, Long timestamp, SignatureInfo sign` |
| `io.openjiuwen.a4p.Models.IntentL2` | Server-issued level-two intent; `String version, IntentL1 intentL1, UserIdentity userIdentity, AgentIdentity agentIdentity, ServiceProviderIdentity serviceProviderIdentity, Long timestamp, SignatureInfo sign` |
| `io.openjiuwen.a4p.Models.IntentL3` | User-signed level-three intent; `String version, IntentL2 intentL2, SigningEnv signingEnv, Long timestamp, SignatureInfo sign` |
| `io.openjiuwen.a4p.Models.Execution` | Execution request and action payload; `IntentActionType action, String id, JsonNode executionContent` |
| `io.openjiuwen.a4p.Models.ExecutionRecord` | Ledger record for reservation, completion or cancellation; `String executionId, ExecutionStatus status, IntentActionType action, JsonNode executionContent, Long periodIndex, long reservedAt, Long completedAt, Long canceledAt` |
| `io.openjiuwen.a4p.Models.IntentActionType` | `AI_PAY` |
| `io.openjiuwen.a4p.Models.AlgorithmType` | `RSA3072` |
| `io.openjiuwen.a4p.Models.IntentExecutionStatus` | `ACTIVE, CONSUMED, REVOKED, EXPIRED` |
| `io.openjiuwen.a4p.Models.ExecutionStatus` | `PENDING, COMPLETED, CANCELED` |

Records expose canonical constructors and standard `equals`, `hashCode` and `toString`. `SignatureInfo` also accepts `(AlgorithmType alg, String keyIdentifier, String sign)`. Enums expose `values()` and `valueOf(String)`. Collections are copied, environment maps are normalized and frozen, and JSON payload accessors return defensive copies. Record construction does not replace lifecycle validation. `IntentExecutionContext` has no public constructor; obtain it through activation or the codec.

## Payment input and cart matching

`PaymentProfile.structuredIntent(resources, constraints)` and `PaymentProfile.execution(id, order)` wrap typed records as action-selected JSON. They do not replace lifecycle validation. Java `null` omits optional record fields from canonical JSON; payment payload decoders reject explicit JSON null values, including nested nulls.

- `totalMaxAmount`, `currency`, `validFrom` and `validUntil` are required; `validFrom <= validUntil`. `singleOrderMaxAmount` and `maxExecutionCount` are optional positive limits. The order currency must exactly equal the authorized currency.
- `anchorTime`, `periodSeconds` and `maxExecutionCountPerPeriod` must be supplied together or all omitted. `anchorTime <= validFrom`; `periodSeconds` is positive, measured in seconds and at most 9007199254740. Violations return `A4P_INVALID_PARAMETER`.
- Whitelist groups and each group's alternatives must be nonempty. Groups form an AND template; products within a group are OR alternatives. Every product must be unique across the entire whitelist, including within a group.
- Each execution must contain exactly one cart entry per group, with exactly that group's quantity. Quantity is a per-execution template, not a lifetime product quota. Missing groups, repeated groups or wrong quantities return `A4P_CART_MISMATCH` (4005). Unauthorized products return `A4P_PRODUCT_NOT_AUTHORIZED` (4007) and are checked before cart shape.
- Product matching compares the complete seller/SPU/SKU record exactly. Seller ID is required; at least one of SPU or SKU must be present and nonblank. Omission of an identifier is not a wildcard.
- Conversation and summary must be nonblank. Conversation length limits and sensitive-data filtering are the agent application's responsibility. Issuance requires trusted user, agent and service-provider identities; signing requires non-null device and app maps.

## State and accounting

| State | Meaning |
| --- | --- |
| Context `ACTIVE` | Accepts new reservations subject to time and quota checks. |
| Context `CONSUMED` | Global amount or count quota exhausted with no PENDING records; set when completing the final pending execution of an ACTIVE context. |
| Context `REVOKED` | Explicitly revoked; rejects new reservations and allows existing PENDING records to be completed or canceled. |
| Context `EXPIRED` | Marked by an expired reservation attempt; rejects new reservations and allows existing PENDING records to be completed or canceled. |
| Execution `PENDING` | Reserves amount, global count and original-period count. |
| Execution `COMPLETED` | Retains quota usage; cannot be finalized again. |
| Execution `CANCELED` | Releases quota usage and retains the record; cannot be finalized again. |

Revocation requires ACTIVE. Finalizing records in REVOKED or EXPIRED contexts preserves that context status. A full period alone does not make a context CONSUMED. Execution IDs and order IDs are unique within a context and remain unavailable for reuse after cancellation. A null execution ID requests UUID generation; read the generated ID from the resulting ledger.

Periodic accounting uses `floor((now - anchorTime) / (periodSeconds * 1000))`. `ExecutionRecord.periodIndex` is assigned at reservation and never moved by completion or cancellation. Pending and completed records occupy quotas; canceled records do not. Finalizing in a later period refreshes that period's snapshot while retaining the original reservation period and completed global usage. Canceling an old-period reservation does not reduce current-period usage.

`executionInfo()` is a snapshot, refreshed at activation and successful reservation/completion/cancellation. Reads and revocation do not advance its period. Unconfigured optional quotas are null in `PaymentExecutionInfo` and omitted from context JSON.

## Signing and verification callbacks

Implement `Signer` and `SignatureVerifier` using application-owned key infrastructure. The SDK supplies canonical UTF-8 JSON bytes: object keys are sorted, array order is preserved and insignificant whitespace is omitted. Only the current layer's outer `sign` field is removed; embedded layer signatures remain covered. Sign the supplied bytes directly.

Return `SignatureInfo` with `alg`, a trusted key identifier and an unpadded Base64URL signature. Resolve verification keys and their bound algorithm through a trusted registry: the outer signature metadata is not covered by its own layer's signature. All signer callback exceptions (including `A4pException`) map to `A4P_SIGNING_FAIL`. Invalid returned signature metadata maps to `A4P_INVALID_PARAMETER`. Verifier exceptions, null verdicts, or disagreement between `valid` and `code == 0` map to `A4P_VERIFY_FAIL`. Lifecycle operations requiring a positive verdict also map a negative verdict to `A4P_VERIFY_FAIL`.

| API | Cryptographic verification performed |
| --- | --- |
| `issueIntent` | L1 agent signature. |
| `signIntent` | L2 intent-center signature. |
| `activateIntent` | L3 user signature. |
| `verifyIntentAuthenticity` | L2 issuer signature, after chain structure and version checks; does not verify the user signature or current execution eligibility. |
| Deserializers | Structural validation only; no cryptographic signature verification. |

See [Serialization and signature rules](serialization_and_signature_rules.md).

## Serialization and persistence

`A4pIntentCodec` is a public concrete class with a public no-argument constructor. Use the provided codec directly; no application implementation or factory is required.

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

The same codec supports L1, L2 and L3 serialization and restoration. Context JSON contains `version`, `intentId`, `authorizedIntent`, `activatedTime`, `status`, `executionInfo` and `executions`. Serialization takes a consistent context snapshot under its lock. Parsing rejects duplicate keys and trailing tokens; structural validation failures may return specific protocol errors rather than only 5002. Intent deserializers check structure; context restoration additionally checks supported versions, activation time, ledger constraints, reachable status and accounting consistency.

Restoration does not authenticate stored data or reverify signatures. Protect persistence integrity at the application level where required. Context JSON is for local persistence only and must not be used as a network message. Old contexts without the required `activatedTime` cannot be restored directly; use an application-controlled trusted migration, or a new authorization lifecycle without resetting existing authorization usage.

## Result codes

| Enum | Code | Message |
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

Code 4003 is retained for ArkTS compatibility; Java uses 4004 for per-period execution-count limits.

## Usage contract

1. Create or restore all Intent and Context structures through SDK APIs. Hand-assembled lifecycle structures may cause undefined behavior. Applications may construct the documented request, identity, environment and order input models.
2. Follow the lifecycle: createIntent → issueIntent → signIntent → activateIntent → createExecution → completeExecution or cancelExecution. An ACTIVE Context may be revoked.
3. Do not repeat the same lifecycle step for an Intent, activation/revocation for a Context, or reservation/finalization for an Execution. An ACTIVE Context may process distinct Executions; reads and serialization may be repeated.
4. Persist the Context after activateIntent and every other API returning a Context. A failed createExecution may change its status to EXPIRED, so persist the current Context as well. Persistence is the caller's responsibility.
5. The SDK does not protect persisted Contexts against tampering or verify their integrity. Add caller-side verification where required; structural checks during deserialization are not security integrity checks.
6. Serialized Contexts are for local persistence only and must not be transmitted as network messages.
