# A4P Java SDK

English | [简体中文](./README_zh.md)

Java 17 implementation of the A4P payment-intent protocol, ported from the ArkTS SDK in
[PR #130](https://github.com/openJiuwen-ai/agent-protocol/pull/130).

## Build and test

Requirements: JDK 17 or later, Maven 3.9 or later.

```sh
cd A4P-java-sdk
mvn test
mvn package
```

### Build script

The scripts require Bash (for example, Linux, WSL or Git Bash on Windows). From the
repository root, run:

```sh
bash A4P-java-sdk/scripts/build.sh          # Default: build
bash A4P-java-sdk/scripts/build.sh build    # Explicit build mode
bash A4P-java-sdk/scripts/build.sh ut       # Run unit tests only
bash A4P-java-sdk/scripts/build.sh --help
```

`build` runs `clean package` with `-Dmaven.test.skip=true`, skipping both test
compilation and execution. The JAR is written to
`A4P-java-sdk/target/a4p-java-sdk-0.1.0.jar`. `ut` runs `clean test`, requires tests to
be present and fails on test failures; it does not package the JAR. Surefire reports
are written to `A4P-java-sdk/target/surefire-reports/`. Both modes clean `target/`
before running and return Maven's exit code.

Additional Maven options go after the mode, for example:

```sh
bash A4P-java-sdk/scripts/build.sh build -o
bash A4P-java-sdk/scripts/build.sh ut -Dtest=Rsa3072Test
```

`-o` uses Maven's offline mode, so the required dependencies and plugins must already
be cached. The script locates `pom.xml` relative to itself, independently of the
caller's working directory.

### UT script with CI reports

To run unit tests and generate CI reports, execute the following from the repository
root. The script locates the repository root relative to itself, independently of
the caller's working directory. Optionally set `WORKSPACE` to an absolute repository
root to override this default.

```sh
bash A4P-java-sdk/scripts/ut.sh
```

This script requires Git, Maven and standard shell utilities, including `grep` with
`-P` support (for example, GNU grep). It runs `mvn clean test` and returns Maven's
exit code (`0` for `PASS`, nonzero for `FAILED`). Reports are written under
`outputs/` under the resolved repository root (`$WORKSPACE/outputs/` when overridden):

| File | Contents |
| --- | --- |
| `ut.log` | Maven test log |
| `unit_test_report.html` | Test counts and the last 200 log lines |
| `junit.xml` | Combined JUnit XML, generated when Surefire XML reports exist |
| `ut.json` | Status, Maven exit code and report link |

Optional environment variables `PR_ID` (default `0`) and `OBS_BUCKET` (default
`openjiuwen-ci`) control the report link in `ut.json`. The script generates local
reports; uploading them to the linked OBS location is a separate CI step.

The artifact coordinates are `io.openjiuwen:a4p-java-sdk:0.1.0`.
Jackson handles JSON; JUnit 5 is test-only. No web framework is required.

`jackson-databind` (2.20.1 here) is the only runtime dependency; JUnit 5 is `test` scope.

```xml
<dependency>
  <groupId>io.openjiuwen</groupId>
  <artifactId>a4p-java-sdk</artifactId>
  <version>0.1.0</version>
</dependency>
```

Run `io.openjiuwen.a4p.examples.PaymentExample` from an IDE, or:

```sh
mvn compile org.codehaus.mojo:exec-maven-plugin:3.6.3:java "-Dexec.mainClass=io.openjiuwen.a4p.examples.PaymentExample"
```

The exec plugin is not declared in `pom.xml`, so that command resolves it from Maven
Central on first use and therefore needs network access.

The example uses separate agent, intent-center and user RSA-3072 key pairs. It creates,
issues, authorizes, activates, verifies, reserves and completes a payment.

## API

| Specification | Java |
| --- | --- |
| `createIntent` | `A4p.createIntent(CreateIntentRequest, Signer[, long now])` returns `A4pResult<IntentL1>` |
| `issueIntent` | `A4p.issueIntent(IntentL1, SignatureVerifier, IntentIssueContext, Signer[, long now])` returns `A4pResult<IntentL2>` |
| `signIntent` | `A4p.signIntent(IntentL2, SignatureVerifier, SigningEnv, Signer[, long now])` returns `A4pResult<IntentL3>` |
| `activateIntent` | `A4p.activateIntent(IntentL3, SignatureVerifier[, long now])` returns `A4pResult<IntentExecutionContext>` |
| `verifyIntentAuthenticity` | `A4p.verifyIntentAuthenticity(IntentL3, SignatureVerifier)` returns `A4pResult<VerificationResult>` |
| `revokeIntent` | `A4p.revokeIntent(IntentExecutionContext)` returns `A4pResult<IntentExecutionContext>` |
| `createExecution / completeExecution / cancelExecution` | Static methods on `A4p`, returning `A4pResult<IntentExecutionContext>` |
| `A4pResult<T>` | `A4pResult`: `success()`, `data()`, `code()` (`long`), `message()` |
| `A4pException` | `A4pException`, thrown only by value-returning helpers and for SDK defects |
| `A4pErrorCode` | `code()`, `message()`, and `fromCode(int)` for turning a result code back into a constant |
| `A4pIntentCodec` | `A4pIntentCodec.standard()`; `serializeIntentL1/L2/L3` and `serializeIntentExecutionContext` return `String`, and each `deserialize…` counterpart returns `A4pResult` |
| `Signer / SignatureVerifier` | Functional interfaces; `Signer.sign(byte[])` returns a `SignatureInfo` |
| `Rsa3072` | Optional adapter: `Rsa3072.signer(String, PrivateKey)` and `Rsa3072.verifier(Map<String, PublicKey>)` |
| `CanonicalJson` | Canonical helpers used when signing: `stringify`, `toSignableBytes`, `encodeUtf8`/`decodeUtf8`, `base64UrlEncode`/`base64UrlDecode`, `MAX_SAFE_INTEGER` |
| Typed union (`StructuredIntent`, `Execution`, `executionInfo`) | `JsonNode` payloads; `PaymentProfile` converts AI_PAY payloads to and from records |
| Data model | Immutable records and enums in `Models` |

`[long now]` marks the explicit-clock overload described below.

The records and enums are nested in `Models`; `A4p`, `A4pResult`, `A4pException`,
`A4pErrorCode`, `A4pIntentCodec`, `IntentExecutionContext`, `PaymentProfile` and `Rsa3072`
are top-level classes, so both imports are needed:

```java
import io.openjiuwen.a4p.*;
import io.openjiuwen.a4p.Models.*;
```

Build AI_PAY payloads through `PaymentProfile`. The sketch below shows the shape of the
calls; `io.openjiuwen.a4p.examples.PaymentExample` is the complete, compiling version,
which also sets up the key pairs, the L1/L2/L3 chain and canonical serialization.

```java
// Activate an intent that the agent created and the intent center issued and signed.
SignatureVerifier userVerifier = Rsa3072.verifier(Map.of("user", userPublicKey));
A4pResult<IntentExecutionContext> activated = A4p.activateIntent(authorizedIntent, userVerifier, now);
if (!activated.success()) { /* branch on activated.code() */ }
IntentExecutionContext context = activated.data();

// Reserve, then complete, one order against the activated intent.
Order order = new Order("order-1", 3000L, "CNY", List.of(new CartItem(product, 1L)), now);
Execution execution = PaymentProfile.execution("execution-1", "1.0", order);
A4pResult<IntentExecutionContext> reserved = A4p.createExecution(context, execution, now);
if (!reserved.success()) { /* branch on reserved.code(), then persist the context */ }
A4pResult<IntentExecutionContext> completed = A4p.completeExecution(context, execution.id(), now);

PaymentExecutionInfo info = PaymentProfile.executionInfo(context);
```

A failed `createExecution` can still have moved the context to `EXPIRED`, so persist the
context after every call. `serializeIntentExecutionContext` /
`deserializeIntentExecutionContext`
on `A4pIntentCodec` provide the persisted form; deserialization re-validates the stored L3
(without re-verifying the user signature), checks that the ledger respects the authorized
constraints, and requires `executionInfo` to match the ledger. It also requires a reachable
state: each `reservedAt` lies within `[validFrom, validUntil]`, `completedAt`/`canceledAt`
are not earlier than `reservedAt`, and the status is `CONSUMED` exactly when nothing is
pending and the global quota is used up (`REVOKED` and `EXPIRED` accept any ledger).

Optional fields use `null`; absent optional fields are omitted from signed JSON. Explicit
`null` values inside AI_PAY payloads are rejected. Monetary values are positive integers in
the application's agreed minor unit. All protocol numbers are integers limited to the
JavaScript safe-integer range; values that are not integers must be carried as strings.

Lifecycle/execution methods have both a system-clock overload and an explicit
`long now` overload (epoch milliseconds). An explicit `now` must be a non-negative
safe integer and is validated before anything else. Production code must supply a
trusted clock, never a client-provided timestamp. Order timestamps do not determine
quota periods or authorization validity.

Required strings must be non-blank (using the JavaScript `trim()` whitespace set). A
product needs a non-blank seller ID and at least one of `spuId`/`skuId`. A periodic rule
requires `anchorTime <= validFrom` and `periodSeconds <= 9007199254740`. Chain timestamps
are monotonic: `L1.timestamp <= L2.timestamp <= L3.timestamp`. Activation requires
`L3.timestamp <= now <= validUntil` (`A4P_INVALID_PARAMETER` / `A4P_TIMESTAMP_EXPIRED`); an
intent that is not yet valid can be activated, and reservations before `validFrom` fail with
`A4P_TIMESTAMP_TOO_EARLY`. Length limits and
sensitive-data filtering of `intentConversation` are the Agent's responsibility.

### Result codes

Callers branch on `code()`; `message()` is a fixed diagnostic text, and the values come
from `A4pErrorCode`, shared with the ArkTS SDK. `0` means success and every other value is
a protocol failure. `A4pErrorCode.fromCode(int)` resolves a code back to its constant and
maps an unknown value to `A4P_INTERNAL_ERROR` (cast the `long` returned by `code()`).

| Code | Constant | Meaning |
| --- | --- | --- |
| 0 | `A4P_SUCCESS` | Operation completed successfully |
| 1001 | `A4P_INVALID_PARAMETER` | Invalid parameter |
| 1002 | `A4P_UNSUPPORTED_VERSION` | Unsupported version |
| 1003 | `A4P_DUPLICATE_ID` | Duplicate ID |
| 2001 | `A4P_SIGNING_FAIL` | Signing failed |
| 2002 | `A4P_VERIFY_FAIL` | Verification failed |
| 3001 | `A4P_INVALID_STATUS` | Invalid status |
| 3002 | `A4P_TIMESTAMP_EXPIRED` | Timestamp expired |
| 3003 | `A4P_TIMESTAMP_TOO_EARLY` | Timestamp too early |
| 4001 | `A4P_EXECUTION_COUNT_EXCEEDED` | Execution count exceeded |
| 4002 | `A4P_AMOUNT_EXCEEDED` | Amount exceeded |
| 4003 | `A4P_PERIOD_COUNT_EXCEEDED` | Reserved for ArkTS error-code compatibility; not returned by Java (per-period execution limits use 4004) |
| 4004 | `A4P_PERIOD_EXECUTION_COUNT_EXCEEDED` | Period execution count exceeded |
| 4005 | `A4P_PRODUCT_QUANTITY_EXCEEDED` | Product quantity exceeded |
| 4006 | `A4P_SINGLE_ORDER_AMOUNT_EXCEEDED` | Single order amount exceeded |
| 4007 | `A4P_PRODUCT_NOT_AUTHORIZED` | Product not authorized |
| 5001 | `A4P_SERIALIZE_FAIL` | Serialization failed |
| 5002 | `A4P_DESERIALIZE_FAIL` | Deserialization failed |
| 6001 | `A4P_INTERNAL_ERROR` | Unexpected internal error |

`2001` and `2002` also cover the optional adapter: `Rsa3072` returns `2002` for a rejected
signature, and a `Signer`/`SignatureVerifier` that throws is mapped to the same codes by
the SDK, as described below.

## Execution semantics

- Pending and completed records both consume amount, count and whitelist quantity.
- Explicit cancellation releases quota; unknown downstream outcomes do not.
- An order's cart must match the whitelist groups one to one: `cartItems` and
  `itemsWhiteList` have the same length, and each cart item is paired with a distinct
  group listing its product. The cart order does not matter; any valid pairing is enough.
- `orderId` is the payment idempotency key: an order ID already in the ledger,
  canceled records included, is rejected with `A4P_DUPLICATE_ID`.
- Duplicate execution IDs remain rejected after cancellation. A provided execution ID
  must not be blank; a generated ID is stored on the ledger record.
- Reservation checks run in a fixed order: request structure (including a non-blank
  provided execution ID); version, action and currency against the authorized intent;
  context status; validity window (the lazy EXPIRED flip); duplicate execution/order IDs;
  then the execution-count cap, the single-order amount cap, the remaining amount, the
  one-to-one cart match, whitelist quantity, and period quota.
- A cart of the wrong size fails with `A4P_INVALID_PARAMETER`; a cart without a valid
  pairing (including a product outside every group) fails with `A4P_PRODUCT_NOT_AUTHORIZED`.
  For quantity, an item still counts against every group that lists its product.
- Revocation blocks reservations but permits pending completion/cancellation.
- A reservation past `validUntil` marks the context EXPIRED, even though the call fails.
  Pending records can still be completed or canceled in EXPIRED status.
- Completion and cancellation reject a `now` earlier than the record's `reservedAt` with
  `A4P_INVALID_PARAMETER`; the record stays pending.
- An ACTIVE intent becomes CONSUMED after a completion leaves no pending record and either
  the count cap is reached or the remaining amount is 0. REVOKED and EXPIRED are terminal.
- `executionInfo()` always declares `leftExecutionCount`, `leftAmount`, `periodIndex` and
  `leftPeriodExecutionCount`; non-applicable figures are `null`.
- Context operations synchronize validation and updates. This protects threads
  sharing one context, **not** multiple processes or independently activated copies.
- `A4pIntentCodec.standard()` returns a stateless shared singleton, so one instance can
  serve every thread.

The ledger is in-memory. The intent center must persist the context inside a database
transaction locked by `intentId`, prevent replay of the same `intentId`, and bind keys to
identities. Activation checks the outer user signature; `verifyIntentAuthenticity` checks
the L3 chain and the intent center's L2 signature, not the user signature, and does not
depend on the context status.

## Wire format and security

See [canonical JSON specification](docs/canonical-json-spec.md). JSON field names follow the
specification (`intentConversation`, `resources`, `UserIdentity.id`), every enum is
serialized by name (`"AI_PAY"`, `"CONSUMED"`, `"PENDING"`), and the protocol version is
`1.0`. HTTP bindings are not part of this SDK until the specification settles them.

Each level's signature covers the level without its whole `sign` object, so that level's
`alg` and `keyIdentifier` are not part of its own signed bytes; the next level's signature
covers them. This is the specified design. `alg` must be `RSA3072`. Verifiers must resolve
keys exclusively from a trusted registry and bind each key to allowed algorithms; never
trust the incoming `alg`.

`Rsa3072` is an optional working adapter using JCA `SHA256withRSA` (PKCS#1 v1.5),
3072-bit RSA keys and unpadded Base64URL. Its `VerificationResult.code` is `0` on success
and `2002` on failure, with the reason in `message`. Its factories throw
`A4pException(A4P_INVALID_PARAMETER)` for a missing trust map, a blank key identifier or a
missing or non-3072-bit key. Peers must agree on this suite or
supply matching `Signer`/`SignatureVerifier` implementations. Any verifier must return a
`VerificationResult` whose `valid` is true exactly when `code` is `0`; an inconsistent
result is treated as a verification failure.

Intent records and nested payloads are immutable; `JsonNode` payloads are copied on the
way in and out. The Java port also rejects duplicate JSON keys, unknown model fields,
invalid UTF-8, fractional integer fields, numeric enum values, scalar coercions and invalid
callback results instead of silently normalizing security-sensitive input.

## Version and compatibility

The protocol version is `1.0` (`A4p.PROTOCOL_VERSION`). Every level this SDK creates is
stamped with that constant, and each step validates the levels it receives, rejecting a
mismatch with `A4P_UNSUPPORTED_VERSION`. The port implements the same wire
format as the ArkTS SDK (`A4P-arkts-sdk/`) it came from, and the canonical-vector tests
keep the two in step, so intents and persisted contexts are interchangeable across the
two SDKs as long as both use the canonical JSON rules and the same signature suite. The
artifact version is `0.1.0`; protocol compatibility is governed by the `version`
field, not by the artifact version.

## Known limitations

- The ledger is in-memory and per-context. Durability, locked persistence, replay
  prevention and key-to-identity binding belong to the intent center.
- Synchronization covers threads that share one `IntentExecutionContext`, not several
  processes or independently activated copies of the same `intentId`.
- HTTP bindings are out of scope until the specification settles them; only in-process
  calls and canonical JSON serialization are provided.
- `Rsa3072` is one agreed suite rather than a negotiation; peers must either use it or
  supply matching `Signer`/`SignatureVerifier` implementations.
- `verifyIntentAuthenticity` deliberately does not verify the user signature.
- The port is stricter than the protocol minimum in places (duplicate JSON keys, unknown
  fields, numeric enums, scalar coercions) so that security-sensitive input is never
  silently normalized. The alignment documents below record what changed, and why.

## More documentation

Additional reference documents live under `docs/`:

| Document | Contents |
| --- | --- |
| [canonical-json-spec.md](docs/canonical-json-spec.md) | Canonical JSON and Base64URL rules behind every signature. |

## Tests

Tests cover lifecycle, quota rollback, order idempotency, one-to-one cart matching, check
ordering, expiry finalization, terminal states, consumption, whitelist checks, period
boundaries, blank/structural validation, the result model, the typed-union payloads, wire
names and enum encoding, concurrent reservations, canonical vectors, detached signature
metadata, malformed input, verifier result consistency, execution-context persistence and
restore (including reachable status and record timestamps) and real RSA signature
verification/tampering. The digest signer in the test fixtures is only a deterministic
test double and must not be used for authorization.
