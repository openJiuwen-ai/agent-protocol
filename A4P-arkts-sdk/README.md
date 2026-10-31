# A4P ArkTS SDK

English | [简体中文](./README_zh.md)

HarmonyOS (ArkTS) implementation of the A4P (Agent-to-Agent Payment) payment-intent
protocol, delivered as a HAR library (module name `a4p`). It covers the signed intent
lifecycle (create → issue → authorize → activate), payment execution accounting against
the frozen authorization snapshot, and the canonical JSON codec used for signing and
persistence. Cryptography is pluggable through the `Signer` / `SignatureVerifier`
interfaces. The Java port in [`A4P-java-sdk/`](../A4P-java-sdk) was ported from this SDK.

## Build and test

Requirements: DevEco Studio 5.0 or later with HarmonyOS SDK `5.0.0(12)` (API 12).
Command-line builds additionally need `ohpm`, `hvigorw` and Bash (Git Bash on Windows
works); the script also packages the on-device test HAP, which requires `java` (DevEco
Studio's bundled JBR works). The library itself has no third-party dependencies
(`@ohos/hypium` is a test-only dev dependency).

The build script always cleans previous outputs first, then runs `ohpm install`,
`hvigor clean`, `assembleHar` (release) and `genOnDeviceTestHap`:

```sh
scripts/build.sh                                  # resolve ohpm/hvigorw from PATH
scripts/build.sh --ohpm <path> --hvigorw <path>   # or pass explicit locations
```

Artifacts:

- HAR: `a4p/build/default/outputs/default/a4p.har`
- On-device test HAP: `a4p/build/default/outputs/ohosTest/a4p-ohosTest-unsigned.hap`

Tests run in DevEco Studio against `a4p/src/ohosTest`, or on a device from the built
test HAP. There is no example app; the test sources double as reference usage (see
[Tests](#tests)).

### Consuming the library

Add the module (source path or built HAR) to the consumer module's `oh-package.json5`
and run `ohpm install`:

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

Everything is exported from the single `Index.ets` entry: the `A4p` and `A4pCodec`
namespaces, the `Signer` / `SignatureVerifier` interfaces, and the data models.

## API

| Specification | ArkTS |
| --- | --- |
| `createIntent` | `A4p.createIntent(CreateIntentRequest, Signer[, now?])` returns `IntentL1` |
| `issueIntent` | `A4p.issueIntent(IntentL1, SignatureVerifier, IntentIssueContext, Signer[, now?])` returns `IntentL2` |
| `signIntent` | `A4p.signIntent(IntentL2, SignatureVerifier, SigningEnv, Signer[, now?])` returns `IntentL3` |
| `activateIntent` | `A4p.activateIntent(IntentL3, SignatureVerifier[, now?])` returns `IntentExecutionContext` |
| `verifyIntentAuthenticity` | `A4p.verifyIntentAuthenticity(IntentL3, SignatureVerifier)` returns `VerificationResult` |
| `revokeIntent` | `A4p.revokeIntent(IntentExecutionContext[, now?])` returns `IntentExecutionContext` |
| `createExecution / completeExecution / cancelExecution` | Functions on the `A4p` namespace, returning `IntentExecutionContext` |
| Failure reporting | No result wrapper: APIs return data directly and throw a `BusinessError`-shaped error whose `code` is an `A4pErrorCode` (the concrete class is module-internal; catch structurally) |
| Codec | `A4pCodec.serializeIntentL1/L2/L3/IntentExecutionContext` return `String`; each `deserialize…` counterpart validates the structure and returns the object |
| Crypto | None bundled: `Signer.sign(Uint8Array): SignatureInfo` and `SignatureVerifier.verify(Uint8Array, SignatureInfo): VerificationResult` are supplied by the application |
| Data model | Plain interfaces and string enums (`AI_PAY`, `RSA3072`, `ACTIVE`, `PENDING`) |

`now?` marks the optional explicit-clock parameter described below.

The sketch below shows the shape of the calls; the ohosTest sources (in particular
`A4p.test.ets` and `mock/TestDataFactory.ets`) are complete, compiling references.

```typescript
// The agent creates and signs L1; the server verifies it, issues and signs L2
// with the three identities; the user verifies L2 and signs L3.
const intentL1 = A4p.createIntent(request, agentSigner);
const intentL2 = A4p.issueIntent(intentL1, agentVerifier, issueContext, serverSigner);
const intentL3 = A4p.signIntent(intentL2, serverVerifier, signingEnv, userSigner);

// Activate: verify the user's L3 signature and freeze the authorization snapshot.
let context = A4p.activateIntent(intentL3, userVerifier, now);

// Reserve, then complete, one order against the activated intent.
context = A4p.createExecution(context, execution, now);
context = A4p.completeExecution(context, executionId, now);

// Failures throw structurally BusinessError-shaped errors; branch on the code.
try {
  context = A4p.createExecution(context, execution, now);
} catch (e) {
  const err = e as BusinessError<number>;
  if (err.code === A4pErrorCode.A4P_AMOUNT_EXCEEDED) { /* budget exhausted */ }
}
```

A failed `createExecution` can still have moved the context to `EXPIRED` (the lazy flip
described below), so persist the context after every call.
`A4pCodec.serializeIntentExecutionContext` / `deserializeIntentExecutionContext` provide
the persisted form; deserialization re-validates the whole stored structure — the
authorized L3 chain, the `intentId` derivation, every ledger record (status-bound
finalization timestamps, a well-formed flat `action`/`executionContent`, `periodIndex`
presence matching the intent's periodicity) and the accounting snapshot (period figures and
`leftExecutionCount` present exactly when their caps apply) — and re-freezes the
authorization snapshot.

Optional fields are `undefined`; absent optional fields are omitted from signed JSON.
Monetary amounts are positive integers in the application's agreed minor unit. All
protocol numbers must be integers within the JavaScript safe-integer range; values that
are not integers must be carried as strings. Values handed to the SDK must be plain
objects (object literals), not class instances — canonical serialization rejects class
instances with `A4P_SERIALIZE_FAIL`.

Lifecycle and execution functions all take an optional explicit clock (`now`, epoch
milliseconds) and default to `Date.now()`. An explicit `now` must be a non-negative safe
integer. Use it for reproducible signed bytes in tests and for caller-side clock-skew
policies; the SDK itself applies no tolerance, and production code must derive `now`
from a trusted clock, never from a client-provided timestamp. Order timestamps are
recorded, not evaluated: quota periods and validity are judged against `now`.

Required strings must be non-blank (JavaScript `trim()` whitespace). A product needs a
non-blank seller id and at least one non-blank `spuId`/`skuId`; seller id + spuId +
skuId is the whitelist matching key, and a product may appear at most once per group and
in at most one group. A period rule requires all of `anchorTime`, `periodSeconds` and
`maxExecutionCountPerPeriod`, with `anchorTime <= validFrom` and
`periodSeconds <= 9007199254740`. Chain timestamps are monotonic:
`L1.timestamp <= L2.timestamp <= L3.timestamp <= activation`. The validity window is
judged progressively as the chain approaches execution: issuance applies no window
check; signing and activation refuse an instant past `validUntil` with
`A4P_TIMESTAMP_EXPIRED` (an L3 or a context created there could never reserve) but
permit one before `validFrom`, the intent simply waiting for the window to open; the
full window — both ends plus the `activatedTime` floor — is enforced at reservation
time, where an early reservation fails with `A4P_TIMESTAMP_TOO_EARLY` and a late one
flips the context to EXPIRED. Length limits and sensitive-data filtering of
`intentConversation` are the Agent's responsibility.

### Result codes

Callers branch on the numeric `code`; the message is a fixed diagnostic text per code
(the specific cause is logged via hilog at the throw site). The values come from the
`A4pErrorCode` enum. `0` means success and every other value is a protocol failure.

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
| 4003 | `A4P_PERIOD_COUNT_EXCEEDED` | Defined for code-table compatibility; never thrown by this SDK (per-period limits report 4004) |
| 4004 | `A4P_PERIOD_EXECUTION_COUNT_EXCEEDED` | Period execution count exceeded |
| 4005 | `A4P_CART_MISMATCH` | Cart does not match the authorized resource |
| 4006 | `A4P_SINGLE_ORDER_AMOUNT_EXCEEDED` | Single order amount exceeded |
| 4007 | `A4P_PRODUCT_NOT_AUTHORIZED` | Product not authorized |
| 5001 | `A4P_SERIALIZE_FAIL` | Serialization failed |
| 5002 | `A4P_DESERIALIZE_FAIL` | Deserialization failed |
| 6001 | `A4P_INTERNAL_ERROR` | Unexpected internal error |

`2001` and `2002` also cover the crypto boundary: a `Signer`/`SignatureVerifier` that
throws, returns nothing, or returns a malformed or self-contradictory result (`valid`
true with a non-success code, or `valid` false with code `0`) is mapped to these codes
by the SDK, so raw exceptions never leak through the lifecycle APIs.

## Execution semantics

- Pending and completed records both consume amount, count and period quota; only an
  explicit cancel releases quota. Unknown downstream outcomes do not roll back.
- The cart must match the whitelist template exactly: `cartItems` and `itemsWhiteList`
  have the same length, each cart item is paired with a distinct group listing its
  product (OR within a group, AND across groups), and its quantity equals the group's
  `quantity`. The cart order does not matter. A wrong size, a group matched twice, a
  wrong quantity or an uncovered group fails with `A4P_CART_MISMATCH`; a product outside
  every group fails with `A4P_PRODUCT_NOT_AUTHORIZED`.
- `orderId` is the payment idempotency key: an order id already in the ledger, canceled
  records included, is rejected with `A4P_DUPLICATE_ID`. Duplicate execution ids remain
  rejected after cancellation; a provided execution id must be non-blank, an absent one
  is generated.
- Reservation checks run in a fixed order: request structure; action and currency
  against the authorized intent; context status; validity window (`validFrom`, the
  `activatedTime` floor, the lazy EXPIRED flip); duplicate execution/order ids; the
  execution-count cap, the single-order cap, the remaining amount; whitelist scope and
  template; period quota.
- `amount` is caller-declared: the SDK validates it against the signed caps but never
  reconciles it with the cart (`Product` carries no price). Binding the amount to actual
  prices is the responsibility of the party holding the authoritative product catalog.
- Revocation requires an ACTIVE context and blocks reservations, but pending executions
  can still complete or cancel.
- A reservation past `validUntil` flips the context to EXPIRED even though the call
  fails; pending records can still be finalized in EXPIRED and REVOKED states.
- An ACTIVE context becomes CONSUMED when a completion leaves no pending record and
  either the count cap is reached or the amount budget is exhausted. REVOKED and
  EXPIRED are terminal.
- `executionInfo` always provides `leftAmount`; `periodIndex`/`leftPeriodExecutionCount`
  appear only for periodic intents and `leftExecutionCount` only when `maxExecutionCount`
  is present — non-applicable figures are omitted, not null. The snapshot is always
  re-derived from the ledger, never maintained by hand.
- The caller's execution object is neither mutated nor aliased: the id is resolved
  locally and stored as the record's `executionId`, and a deep copy of the execution
  content lands on the flat ledger record after every check has passed. The ledger is
  append-only.
- Completion books no quota but re-derives the accounting snapshot at the evaluation
  instant, so the period figures follow `now`, not the last ledger mutation.
- The authorized intent is deep-cloned and deep-frozen at activation; execution checks
  read the immutable snapshot while the ledger and figures stay mutable.
- Context operations carry no locks. That is safe in the ArkTS single-threaded runtime,
  **not** across processes or independently activated copies of the same `intentId`.
- `verifyIntentAuthenticity` structurally validates the whole L3 chain (its own `sign`,
  `signingEnv`, `timestamp` and `version` included) and verifies the server's L2
  signature, not the user signature; it does not depend on any context status.

The ledger is in-memory. The integrating service (the intent center) must persist the
context inside a database transaction locked by `intentId`, prevent replay of the same
`intentId`, and bind keys to identities.

## Wire format and security

The canonical JSON and Base64URL rules behind every signature are the ones documented in
[canonical-json-spec.md](../A4P-java-sdk/docs/canonical-json-spec.md): keys sorted by
UTF-16 code units, no whitespace, finite safe integers only, `undefined` omitted,
signing strips only the complete top-level `sign` property (nested chain signatures
stay), UTF-8 without BOM, and RFC 4648 Base64URL without padding, with the decoder
rejecting padding, non-URL-safe characters and nonzero unused trailing bits. JSON field
names are the interface property names (`intentConversation`, `structuredIntent.resources`,
`userIdentity.id`), every enum is serialized by name (`"AI_PAY"`, `"CONSUMED"`,
`"PENDING"`), and the protocol version is `1.0`.

Each level's signature covers the level without its whole `sign` object, so that level's
`alg` and `keyIdentifier` are not part of its own signed bytes; the next level's
signature covers them. This is the specified design. `alg` must be `RSA3072` (validated
structurally, since `alg` is detached metadata). Verifiers must resolve keys exclusively
from `keyIdentifier` against keys they trust for the role, and must reject any `alg`
incompatible with the resolved key type — never trust the incoming `alg`. This is the
primary defense against algorithm- and key-confusion attacks (the same class as unsigned
JWT headers, cf. RFC 8725).

The SDK bundles no cryptographic implementation. For the `RSA3072` suite, supply
`Signer`/`SignatureVerifier` built on `@kit.CryptoArchitectureKit` using
`SHA256withRSA` (PKCS#1 v1.5) with 3072-bit keys and unpadded Base64URL, so peers
interoperate with the Java SDK's `Rsa3072` adapter.

Caller-supplied objects are deep-cloned into intents (never aliased), and the activated
snapshot is deep-frozen. An internal, unexported `A4pHttp` module builds and parses
request/response envelopes for the three protocol endpoints
(`/a4p/v1/intent/proposal|authorization|submission`, media type `application/a4p+json`)
and maps every error code to an HTTP status (401 signature failures, 409 state and quota
conflicts, 426 unsupported version, 400 parameter errors, 500 the rest); it is exercised
by tests but not part of the public API until the specification settles the bindings.

## Version and compatibility

The protocol version is `1.0`. Every level this SDK creates is stamped with it, and each
step validates the levels it receives, rejecting a mismatch (including mixed-version
chains) with `A4P_UNSUPPORTED_VERSION`. The HAR version is `1.0.0`; protocol
compatibility is governed by the `version` field, not by the HAR version.

## Known limitations

- The ledger is in-memory and per-context. Durability, locked persistence, replay
  prevention and key-to-identity binding belong to the intent center.
- No cryptographic implementation is bundled; applications supply `Signer` /
  `SignatureVerifier`. The XOR-based `MockSigner` in the test sources is a deterministic
  test double and must not be used for authorization.
- HTTP bindings are internal and unexported until the specification settles them; only
  in-process calls and canonical JSON serialization are public.
- `verifyIntentAuthenticity` deliberately does not verify the user signature.
- Deserialization is structural (JSON.parse semantics): unknown fields and duplicate
  JSON keys are not rejected, unlike the stricter Java port.
- Signing and activation judge only the window's upper bound (`validUntil`); the
  window's opening (`validFrom`) and the lazy EXPIRED flip are left to reservation
  time.
- Context operations are unsynchronized: safe in the single-threaded ArkTS runtime
  only, not across processes or independently activated copies.

## More documentation

| Document | Contents |
| --- | --- |
| [canonical-json-spec.md](../A4P-java-sdk/docs/canonical-json-spec.md) | Canonical JSON and Base64URL rules behind every signature (the rules this SDK implements). |

## Tests

On-device hypium suites under `a4p/src/ohosTest` (300+ cases):

- `A4p.test.ets` — lifecycle (create/issue/sign/activate/verify/revoke), blank and
  structural validation, signer/verifier failure guards, execution accounting, quota
  caps and rollback, order idempotency, cart template matching, period boundaries,
  check ordering, expiry finalization and terminal states.
- `A4pCodec.test.ets` — deterministic and key-order-independent serialization, the
  golden wire form at a fixed clock, round-trips, and context restore (including
  re-freezing the authorized snapshot).
- `A4pHttp.test.ets` — envelope builders and parsers, the complete error-code →
  HTTP-status table, and the proposal → authorization → submission flow.
- `Utils.test.ets` — base64url, UTF-8 and canonical JSON edge cases, including
  rejection of non-canonical encodings.

Run them in DevEco Studio against `a4p/src/ohosTest`, or install the built test HAP on
a device. The test doubles (`MockSigner`, `MockVerifier`, `TestDataFactory`) are
deterministic fakes for tests only, not authorization-grade cryptography.
