# Java SDK public API

[English](API.md) | [简体中文](API_zh.md)

## Usage contract

1. Create or restore every Intent and Context through SDK APIs. Hand-assembled lifecycle structures may cause undefined behavior. Construct request, identity, environment and order input models as documented below.
2. Follow the lifecycle: createIntent → issueIntent → signIntent → activateIntent → createExecution → completeExecution or cancelExecution. An ACTIVE context may be revoked.
3. Do not repeat a lifecycle step for the same intent, activation/revocation for the same context, or reservation/finalization for the same execution. An ACTIVE context may process distinct executions; reads and serialization may be repeated.
4. Persist the context after activateIntent and every other API returning a context. A failed createExecution may change status to EXPIRED, so persist that context as well. Persistence is the caller responsibility.
5. The SDK does not authenticate persisted contexts or guarantee integrity against corruption or tampering. Add caller-side integrity verification when required; decoder structural checks are not security integrity checks.
6. Serialized contexts are for local persistence only and must not be transmitted as network messages.

## Time and compatibility

All timestamps and now values use Unix epoch milliseconds. activatedTime records the activation instant: Java long or ArkTS safe-integer number. Create, complete, cancel and revoke reject evaluation before activation; the same millisecond is allowed. Omitting now uses the system clock; supplying it sets the evaluation instant. Completion refreshes executionInfo for the completion period without moving reservations between periods. activatedTime is required; older contexts without it cannot be directly restored. Reactivate or define an application-controlled trusted migration.

Java lifecycle APIs return A4pResult<T>: check success() before reading data(). Issue, sign and activation timestamps earlier than the preceding intent timestamp return A4P_INVALID_PARAMETER. Reservation or revocation before activatedTime, and completion or cancellation before reservedAt, return A4P_TIMESTAMP_TOO_EARLY. Serialization and some utility APIs throw A4pException. Overloads without now use System.currentTimeMillis().

## Functions

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
| `A4pErrorCode` | `String message()` | Read the fixed message associated with the code. |
| `A4pErrorCode` | `static A4pErrorCode fromCode(int code)` | Map a numeric code to its enum; unknown codes map to internal error. |
| `A4pException` | `A4pException(A4pErrorCode code)` | Construct a runtime exception with a code and optional message and cause. |
| `A4pException` | `A4pException(A4pErrorCode code, String message, Throwable cause)` | Construct a runtime exception with a code and optional message and cause. |
| `A4pException` | `A4pErrorCode code()` | Read the result code or exception error code. |
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
| `A4pResult` | `String message()` | Read the fixed message associated with the code. |
| `A4pResult` | `String toString()` | Return a result summary without business payloads. |
| `A4pResult` | `A4pResult(boolean success, T data, long code, String message)` | Construct a result in an application codec; success requires non-null data and code 0, failure requires null data and a nonzero code. |
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

Java places the protocol records and enums in `io.openjiuwen.a4p.Models`. Import individual types as `io.openjiuwen.a4p.Models.IntentL1` (and likewise for each full-path row below), or use `import io.openjiuwen.a4p.Models.*;`. `import io.openjiuwen.a4p.A4p` imports only the lifecycle class and cannot import these nested types. `PaymentProfile` is retained as a public convenience API for constructing and decoding AI_PAY payloads.

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

Every record exposes its canonical constructor, named component accessors and standard equals/hashCode/toString. Accessors read the fields above. Public IntentL1/L2/L3 constructors do not authorize bypassing SDK lifecycle creation. Collections are copied; JSON access is defensive. SignatureInfo also accepts AlgorithmType in a constructor. PROTOCOL_VERSION is "1.0"; A4pErrorCode defines the result-code constants; enums provide standard values()/valueOf(String).

## Codec implementation and persistence

Implement `A4pIntentCodec` in the application and retain that instance for local persistence. The SDK's standard codec is package-private; the interface has no public factory. The source test program `CodecConformanceCheck` compares an implementation with the internal standard codec on representative L1, L2, L3 and Context values. Run it with the implementation class on the test classpath; passing these vectors is not a proof of full compatibility.

From the repository root, after placing the implementation on the test classpath:

```sh
mvn -f A4P-java-sdk/pom.xml test-compile org.codehaus.mojo:exec-maven-plugin:3.6.3:java -Dexec.classpathScope=test -Dexec.mainClass=io.openjiuwen.a4p.CodecConformanceCheck -Dexec.args=your.package.YourIntentCodec
```

```java
A4pIntentCodec codec = new MyIntentCodec(); // application implementation
String json = codec.serializeIntentExecutionContext(context);
A4pResult<IntentExecutionContext> result = codec.deserializeIntentExecutionContext(json);
```

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
| `A4P_PRODUCT_QUANTITY_EXCEEDED` | 4005 | Product quantity exceeded |
| `A4P_SINGLE_ORDER_AMOUNT_EXCEEDED` | 4006 | Single order amount exceeded |
| `A4P_PRODUCT_NOT_AUTHORIZED` | 4007 | Product not authorized |
| `A4P_SERIALIZE_FAIL` | 5001 | Serialization failed |
| `A4P_DESERIALIZE_FAIL` | 5002 | Deserialization failed |
| `A4P_INTERNAL_ERROR` | 6001 | Unexpected internal error |

## Inputs and states

- IntentIssueContext supplies identities for issuance; it is not an activated execution context. SigningEnv supplies device and app information. Applications may construct these input structures.
- Protocol numbers must be integers within ±9007199254740991. Timestamps are non-negative; quantities and amounts must meet their validators. Currencies must agree. Supply anchorTime, periodSeconds and maxExecutionCountPerPeriod together, or omit all three. periodSeconds uses seconds and must not exceed 9007199254740. anchorTime must be no later than validFrom; violating this constraint returns A4P_INVALID_PARAMETER when creating or validating the intent.
- ACTIVE accepts reservations. REVOKED and EXPIRED reject new reservations but allow finalizing existing PENDING records. CONSUMED means global quota is exhausted with no pending records. COMPLETED and CANCELED are terminal execution states and cannot be finalized again. Canceling retains the ledger record.
- executionInfo reflects the latest relevant evaluation instant and does not refresh on reads. Completing across periods neither moves the old execution periodIndex nor releases completed orders' total amount or execution-count usage.
