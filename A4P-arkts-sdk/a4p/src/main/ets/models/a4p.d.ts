/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

import { Signer } from '../api/signer';
import { SignatureVerifier } from '../api/verifier';

/**
 * Declaration-only contract mirror of the A4P public surface (no runtime
 * code). The runtime models in this directory and the runtime APIs in
 * ../api/a4p.ts and ../api/codec.ts are the single source of truth; any
 * change to them MUST be mirrored here in the same change.
 */

/**
 * Actions an intent can authorize.
 */
export enum IntentActionType {
  /** Agent payment: purchase of whitelisted products under payment constraints */
  AI_PAY = 'AI_PAY',
}

/**
 * Signature algorithms supported for intent signing.
 */
export enum AlgorithmType {
  /** RSA signature with a 3072-bit key */
  RSA3072 = 'RSA3072',
}

/**
 * Structured intent data defining the action, the authorized resource,
 * and the payment constraints.
 */
export interface StructuredIntent {
  /** Action to be performed */
  action: IntentActionType;
  /** Resource being acted upon (the authorized purchase whitelist) */
  resource: ShoppingItems;
  /** Payment constraints for this intent */
  constraints: PaymentConstraints;
}

/**
 * Detached signature metadata attached to each intent level: the algorithm
 * and key identifier used to sign this level, plus the signature itself.
 * None of these fields are part of the signed bytes.
 */
export interface SignatureInfo {
  /** Signing algorithm used; detached metadata, not covered by the signature (see sign) */
  alg: AlgorithmType;
  /** Key index used for signing; detached metadata, not covered by the signature (see sign) */
  keyIdentifier: string;
  /**
   * Signer's signature (base64url-encoded, unpadded) over the canonical
   * JSON of the intent with the top-level `sign` object removed entirely.
   * `alg`/`keyIdentifier` sit inside that removed object, so they are NOT
   * covered by the signature: verification reads them before verifying and
   * cannot rely on the signature to authenticate them — the verifier
   * contract (key resolution from `keyIdentifier`, `alg` bound to the key
   * type) is the defense against alg/key confusion.
   */
  sign: string;
}

/**
 * Level 1 Intent - Created by an agent.
 * This is the initial state of an intent before issuance.
 */
export interface IntentL1 {
  /** Intent version */
  version: string;
  /** Unique intent identifier */
  intentId: string;
  /** Intent conversation configuration */
  intentConversation: string;
  /** Human-readable intent summary */
  intentSummary: string;
  /** Structured intent data */
  structuredIntent: StructuredIntent;
  /** Creation timestamp (milliseconds since epoch); must be a non-negative integer */
  timestamp: number;
  /** Agent's signing information, including the signature over this intent */
  sign?: SignatureInfo;
}

/**
 * Level 2 Intent - Issued with identities.
 * Created when a Level 1 intent is issued with user, agent, and service provider information.
 */
export interface IntentL2 {
  /** Intent version */
  version: string;
  /** The original Level 1 intent */
  intentL1: IntentL1;
  /** User identity */
  userIdentity: UserIdentity;
  /** Agent identity */
  agentIdentity: AgentIdentity;
  /** Service provider identity */
  serviceProviderIdentity: ServiceProviderIdentity;
  /** Issuance timestamp (milliseconds since epoch); must be a non-negative integer no earlier than the embedded intentL1's timestamp */
  timestamp: number;
  /** Server's signing information, including the signature over this intent */
  sign?: SignatureInfo;
}

/**
 * Level 3 Intent - User-signed and authorized.
 * Created when a user signs a Level 2 intent, authorizing execution.
 */
export interface IntentL3 {
  /** Intent version */
  version: string;
  /** The Level 2 intent */
  intentL2: IntentL2;
  /** Signing environment information */
  signingEnv: SigningEnv;
  /** Signing timestamp (milliseconds since epoch); must be a non-negative integer no earlier than the embedded intentL2's timestamp */
  timestamp: number;
  /** User's signing information, including the signature over this intent */
  sign?: SignatureInfo;
}

/**
 * Payment constraints for an intent: per-order and total amount limits,
 * validity window, execution count cap, and per-period execution limits.
 * `singleOrderMaxAmount` and `maxExecuteCount` are optional (absent means
 * no cap of that kind); the period rule (`anchorTime`/`periodSeconds`/
 * `maxExecutionCountPerPeriod`) is all-or-nothing — a periodic intent
 * provides all three, a non-periodic intent omits all three.
 */
export interface PaymentConstraints {
  /**
   * Maximum amount allowed per single order (in cents); must be a positive
   * integer when present — absent means no per-order amount cap
   */
  singleOrderMaxAmount?: number;
  /** Maximum total amount across all non-canceled executions (in cents); must be a positive integer */
  totalMaxAmount: number;
  /** Currency code for the amounts (e.g., "CNY", "USD") */
  currency: string;

  /** Earliest allowed execution time (milliseconds since epoch); must be a non-negative integer */
  validFrom: number;
  /** Latest allowed execution time (milliseconds since epoch); must be a non-negative integer */
  validUntil: number;
  /**
   * Maximum number of non-canceled executions (an explicit cancel releases
   * a slot); must be a positive integer when present — absent means no
   * execution count cap
   */
  maxExecuteCount?: number;
  /**
   * Anchor time for period calculation (milliseconds since epoch); provided
   * together with periodSeconds and maxExecutionCountPerPeriod, or all
   * three omitted. Must not be later than validFrom: reservations happen at
   * now >= validFrom, so a later anchor would give pre-anchor reservations
   * negative period indexes, each with a fresh per-period budget
   */
  anchorTime?: number;
  /**
   * Period duration in seconds; provided together with anchorTime and
   * maxExecutionCountPerPeriod, or all three omitted. Capped at
   * 9007199254740 (MAX_SAFE_INTEGER / 1000): the millisecond conversion
   * used for period indexes must stay a safe integer
   */
  periodSeconds?: number;
  /**
   * Maximum number of non-canceled executions allowed per period; must be a
   * positive integer when present; provided together with anchorTime and
   * periodSeconds, or all three omitted
   */
  maxExecutionCountPerPeriod?: number;
}

/**
 * Represents a payment order.
 */
export interface Order {
  /**
   * Unique order identifier; doubles as the payment idempotency key —
   * createExecution rejects an orderId already present in the context's
   * ledger (canceled records included)
   */
  orderId: string;
  /** Payment amount in cents; must be a positive integer */
  amount: number;
  /** Currency code (e.g., "CNY", "USD") */
  currency: string;
  /** Shopping cart contents */
  cartItems: CartItem[];
  /** Order creation timestamp (milliseconds since epoch); must be a non-negative integer */
  timestamp: number;
}

/**
 * An execution requested against an activated intent: reserved via
 * createExecution and recorded in the ledger as an ExecutionRecord.
 */
export interface Execution {
  /** Action to perform; must match the authorized intent's action */
  action: IntentActionType;
  /** Unique execution identifier */
  id?: string;
  /** Execution content (order details) */
  executionContent: Order;
}

/**
 * Status of an intent's overall execution lifecycle.
 */
export enum IntentExecutionStatus {
  /** Intent is active and can accept executions */
  ACTIVE = 'ACTIVE',
  /** Intent quota is consumed; no further executions can be reserved */
  CONSUMED = 'CONSUMED',
  /** Intent has been revoked */
  REVOKED = 'REVOKED',
  /** Intent has expired */
  EXPIRED = 'EXPIRED'
}

/**
 * Status of an individual execution.
 */
export enum ExecutionStatus {
  /** Execution is pending */
  PENDING = 'PENDING',
  /** Execution is complete */
  COMPLETED = 'COMPLETED',
  /** Execution is canceled */
  CANCELED = 'CANCELED'
}

/**
 * Record of a single execution within an intent.
 */
export interface ExecutionRecord {
  /** Unique execution identifier */
  executionId: string;
  /** Current status of the execution */
  status: ExecutionStatus;
  /** Period index of the reservation; present only for periodic intents */
  periodIndex?: number;
  /** Timestamp when execution was reserved (always present) */
  reservedAt: number;
  /** Timestamp when execution was completed (only when status is COMPLETED) */
  completedAt?: number;
  /** Timestamp when execution was canceled (only when status is CANCELED) */
  canceledAt?: number;
  /** The execution that was reserved */
  execution: Execution;
}

/**
 * Accounting snapshot of an execution context, derived from the
 * payment constraints and the executions ledger (never maintained by
 * hand; see internal/accounting.ts). Fields whose cap does not apply are
 * omitted: `periodIndex`/`leftPeriodExecutionCount` for non-periodic
 * intents, `leftExecutionCount` when the intent carries no
 * `maxExecuteCount` cap.
 */
export interface PaymentExecutionInfo {
  /** Index of the period containing the evaluation instant; periodic intents only */
  periodIndex?: number;
  /** Executions still allowed in the current period (an explicit cancel releases a slot); periodic intents only */
  leftPeriodExecutionCount?: number;
  /**
   * Executions still allowed against the count cap (an explicit cancel
   * releases a slot); omitted when the intent carries no maxExecuteCount cap
   */
  leftExecutionCount?: number;
  /** Amount still allowed against the total amount cap (non-canceled records only) */
  leftAmount: number;
}

/**
 * Execution context for an activated intent.
 * Tracks the state of all executions for an intent.
 */
export interface IntentExecutionContext {
  /** Context version */
  version: string;
  /** Intent identifier */
  intentId: string;
  /** The authorized Level 3 intent */
  authorizedIntent: IntentL3;
  /** Current execution status */
  status: IntentExecutionStatus;
  /**
   * List of execution records (append-only ledger; canceled records stay in
   * it). The execution count is derived from the non-canceled records; it is
   * not materialized as a separate field.
   */
  executions: ExecutionRecord[];
  /** Accounting snapshot derived from the constraints and the ledger (see PaymentExecutionInfo and internal/accounting.ts) */
  executionInfo: PaymentExecutionInfo;
}

/**
 * Identity of the paying user, as attested by an identity provider.
 */
export interface UserIdentity {
  /** Identifier of the identity provider attesting this identity */
  identityProvider: string;
  /** User identifier, unique within the provider */
  uid: string;
  /** Additional identity information (JSON string); optional, absent means none */
  additionalInfo?: string;
}

/**
 * Identity of the agent creating the intent and acting for the user.
 */
export interface AgentIdentity {
  /** Identifier of the identity provider attesting this identity */
  identityProvider: string;
  /** Agent identifier, unique within the provider */
  id: string;
  /** Additional identity information (JSON string); optional, absent means none */
  additionalInfo?: string;
}

/**
 * Identity of the service provider processing the payment.
 */
export interface ServiceProviderIdentity {
  /** Service provider identifier */
  id: string;
  /** Additional identity information (JSON string); optional, absent means none */
  additionalInfo?: string;
}

/**
 * Environment of the user's device at signing time, recorded on the
 * Level 3 intent.
 */
export interface SigningEnv {
  /** Device information (e.g. model and OS version) */
  device: object;
  /** Application information (e.g. name and version) */
  app: object;
}

/**
 * Seller information for a product.
 */
export interface SellerInfo {
  /** Unique seller identifier; must not be blank */
  id: string;
}

/**
 * SPU/SKU identifier pair that uniquely identifies a product variant.
 * At least one of spuId/skuId must be provided and non-blank, and a
 * provided one must not be blank: the pair is part of the whitelist
 * matching key (see Product), so blank ids would let products match
 * each other vacuously.
 */
export interface SpuSkuInfo {
  /** Standard Product Unit identifier; must not be blank when provided */
  spuId?: string;
  /** Stock Keeping Unit identifier; must not be blank when provided */
  skuId?: string;
}

/**
 * A purchasable product identified by its seller and SPU/SKU pair.
 * Two products are considered equal when seller id, spuId, and skuId
 * all match (see productMatches in internal/validation.ts).
 */
export interface Product {
  /** Seller information */
  seller: SellerInfo;
  /** SPU/SKU information */
  spusku: SpuSkuInfo;
}

/**
 * One whitelist group of an AI_PAY intent's resource order template:
 * the alternative products the user authorized for the group's slot,
 * plus the quantity the group requires per execution.
 */
export interface ItemWhiteList {
  /**
   * Authorized product whitelist; must not be empty. Any single product
   * of the list may satisfy the group's slot (OR). A product may appear
   * at most once here and in at most one group of the resource, so a
   * product uniquely determines its group
   */
  whiteList: Product[];
  /**
   * Quantity required per execution for this group: every AI_PAY
   * execution's cart carries exactly one item for this group with
   * exactly this quantity. Cumulative bounds across executions come
   * from the intent's constraints (amount/count/period caps), not from
   * this field; must be a positive integer
   */
  quantity: number;
}

/**
 * Resource object of a structured intent: the whitelist groups that
 * together form the order template every AI_PAY execution must satisfy
 * — all groups must be purchased (AND), each group satisfied by any
 * one of its whitelisted products (OR).
 */
export interface ShoppingItems {
  /** Authorized whitelist groups; must not be empty */
  itemsWhiteList: ItemWhiteList[];
}

/**
 * A single shopping-cart entry: a product and the ordered quantity.
 */
export interface CartItem {
  /** The purchased product */
  product: Product;
  /** Ordered quantity; must be a positive integer */
  quantity: number;
}

/**
 * Request payload for creating a new Level 1 intent.
 */
export interface CreateIntentRequest {
  /** original intent conversation */
  intentConversation: string;
  /** Human-readable summary of the intent */
  intentSummary: string;
  /** Structured intent data containing action and constraints */
  structuredIntent: StructuredIntent;
  /** Pre-assigned intent identifier (optional; generated when absent) */
  intentId?: string;
}

/**
 * Context required for issuing a Level 1 intent to Level 2.
 */
export interface IntentIssueContext {
  /** Identity of the user involved */
  userIdentity: UserIdentity;
  /** Identity of the agent creating the intent */
  agentIdentity: AgentIdentity;
  /** Identity of the service provider */
  serviceProviderIdentity: ServiceProviderIdentity;
}

/**
 * Result of a signature verification operation.
 */
export interface VerificationResult {
  /** Whether the signature is valid */
  valid: boolean;
  /** Verification result code (provider-specific numeric code) */
  code: number;
  /** Human-readable result message */
  message?: string;
}

/**
 * Core A4P protocol operations. All functions return their data directly
 * and throw `A4pError` on any failure. `A4pError` follows the HarmonyOS
 * `BusinessError` shape (see `@ohos.base`): it extends `Error` and
 * carries a numeric `code` (an `A4pErrorCode`), so callers may handle it
 * as a `BusinessError<number>`.
 */
export declare namespace A4p {
  /**
   * Creates and signs a Level 1 intent. The signer returns a `SignatureInfo`
   * that is attached as the intent's top-level `sign` field; that whole
   * field is stripped when computing the signable bytes, so `alg`,
   * `keyIdentifier`, and `sign` are all detached from the signed content.
   *
   * `now` is the signing instant (epoch milliseconds) recorded in the
   * intent's `timestamp` field and thereby covered by the signature. Unlike
   * the evaluation instant of the execution APIs it drives no decision —
   * validity windows are judged at reservation time, not at signing time.
   * Defaults to the system clock; pass a fixed value in tests to make the
   * signed bytes reproducible. A provided `now` must be a non-negative
   * safe integer.
   * @param request - The create-intent request containing intent details and constraints
   * @param agentSigner - The signer used to produce the agent's L1 signature
   * @param now - Optional signing instant in epoch milliseconds; defaults to Date.now()
   * @returns The signed Level 1 intent
   * @throws A4pError on validation or signing failure
   */
  function createIntent(request: CreateIntentRequest, agentSigner: Signer, now?: number): IntentL1;

  /**
   * Verifies the agent's L1 signature and issues a Level 2 intent signed by
   * the server.
   *
   * The `alg`/`keyIdentifier` used for verification come from the L1's own
   * `sign` field, which is detached from the signed bytes: they are
   * attacker-influenced inputs the signature cannot authenticate, so the
   * verifier must resolve the key solely from `keyIdentifier` and bind
   * `alg` to the resolved key type (see SignatureVerifier).
   *
   * `now` is the signing instant in epoch milliseconds (see createIntent).
   * It must not be earlier than the embedded L1's `timestamp`: the issuance
   * would violate the chain's monotonic timestamps, so it is rejected here
   * instead of producing an L2 that every downstream validation (signIntent,
   * activateIntent) would refuse.
   * The embedded L1 and the three identities are deep copies — the caller's
   * objects are never aliased into the returned intent.
   * @param intentL1 - The Level 1 intent to verify and endorse
   * @param agentVerifier - The verifier used to check the agent's L1 signature
   * @param issueContext - The issue context carrying user, agent, and service-provider identities
   * @param serverSigner - The signer used to produce the server's L2 signature
   * @param now - Optional signing instant in epoch milliseconds; defaults to Date.now()
   * @returns The signed Level 2 intent with the embedded L1
   * @throws A4pError on validation or verification failure
   */
  export function issueIntent(intentL1: IntentL1, agentVerifier: SignatureVerifier,
    issueContext: IntentIssueContext, serverSigner: Signer,
    now?: number): IntentL2;

  /**
   * Verifies the server's L2 signature and produces the user-signed Level 3
   * intent. See issueIntent for the detached `alg`/`keyIdentifier`
   * verification caveat.
   * `now` is the signing instant in epoch milliseconds (see createIntent).
   * It must not be earlier than the embedded L2's `timestamp`: the
   * authorization would violate the chain's monotonic timestamps, so it is
   * rejected here instead of producing an L3 that activateIntent (and every
   * other receiver validating the chain) would refuse.
   * The embedded L2 and the signing environment are deep copies — the
   * caller's objects are never aliased into the returned intent.
   * @param intentL2 - The Level 2 intent to verify and sign
   * @param serverVerifier - The verifier used to check the server's L2 signature
   * @param signingEnv - The signing environment info for the user's signing context
   * @param userSigner - The signer used to produce the user's L3 signature
   * @param now - Optional signing instant in epoch milliseconds; defaults to Date.now()
   * @returns The signed Level 3 intent with the embedded L2
   * @throws A4pError on validation or verification failure
   */
  export function signIntent(intentL2: IntentL2, serverVerifier: SignatureVerifier,
    signingEnv: SigningEnv, userSigner: Signer,
    now?: number): IntentL3;

  /**
   * Verifies the user's L3 signature and creates the execution context with
   * a frozen authorization snapshot. The full chain (embedded L2/L1,
   * identities, constraints, signingEnv) is structurally validated before
   * verification: activation must not freeze a malformed snapshot. See
   * issueIntent for the detached `alg`/`keyIdentifier` verification caveat.
   *
   * `now` is the activation instant in epoch milliseconds: it fixes the
   * period figures (`periodIndex`, `leftPeriodExecutionCount`) of the
   * initial `executionInfo` snapshot for periodic intents; non-periodic
   * intents derive no field from it. It must not be earlier than the L3's
   * own `timestamp` — the chain's monotonic-timestamp rule (see
   * issueIntent) applied to the activation step: no context can be created
   * for an authorization that did not exist yet at that instant. Defaults
   * to the system clock; pass the same value intended for the execution
   * APIs to keep the activation snapshot and the first reservation in the
   * same period. A provided `now` must be a non-negative safe integer.
   * @param intentL3 - The Level 3 intent to verify and activate
   * @param userVerifier - The verifier used to check the user's L3 signature
   * @param now - Optional activation instant in epoch milliseconds; defaults to Date.now()
   * @returns The execution context with a frozen authorization snapshot
   * @throws A4pError on validation or verification failure
   */
  export function activateIntent(intentL3: IntentL3, userVerifier: SignatureVerifier,
    now?: number): IntentExecutionContext;

  /**
   * Verifies the server's L2 signature of the intent carried by the L3,
   * without creating an execution context: the L2 is the server-issued
   * intent, so its signature is the intent's authenticity — and it covers
   * the whole chain below it, the embedded L1's signature included. The
   * user's L3 signature (the authorization) is verified separately by
   * activateIntent. See issueIntent for the detached
   * `alg`/`keyIdentifier` verification caveat.
   *
   * The L3 is only the carrier here: the sole L3-level check is that it
   * contains the L2 — the L3's own `sign`, `signingEnv`, `timestamp`, and
   * `version` are neither verified nor required. The embedded L2 (with its
   * L1) is structurally validated and version-checked before verification,
   * and the signature is verified over the embedded L2's own bytes, so a
   * tampered L2 carrying a signature over different bytes must fail.
   *
   * Unlike the lifecycle APIs this is a pure query: the verifier's verdict
   * is the answer, so an invalid signature is returned as a
   * VerificationResult with `valid: false` (carrying the verifier's own
   * `code`/`message`) instead of being thrown. Structural problems
   * (malformed L2 chain, unsupported L2/L1 version, serialization failure)
   * and a throwing verifier still throw.
   * @param intentL3 - The Level 3 intent carrying the Level 2 intent to verify
   * @param serverVerifier - The verifier used to check the server's L2 signature
   * @returns The verifier's result, including invalid (`valid: false`) verdicts
   * @throws A4pError on structural validation or serialization failure,
   *         or when the verifier itself throws
   */
  export function verifyIntentAuthenticity(intentL3: IntentL3, serverVerifier: SignatureVerifier): VerificationResult;

  /**
   * Revokes the intent: no new executions may be reserved; pending ones
   * can still complete or cancel.
   * @param context - The execution context to revoke
   * @returns The updated execution context with status set to REVOKED
   * @throws A4pError when the intent is not active
   */
  export function revokeIntent(context: IntentExecutionContext): IntentExecutionContext;

  /**
   * Reserves an execution against the context: the order is validated
   * against the frozen authorization snapshot (scope, order template,
   * amounts, caps, period), appended to the ledger as PENDING, and the
   * quota figures are re-derived from the ledger. The caller's execution
   * object is never mutated — the id is resolved locally (generated when
   * absent) and lands on the ledger's own deep copy only after every
   * check has passed.
   *
   * `orderId` is the payment idempotency key: a reservation whose orderId
   * is already present in the context's ledger — canceled records included
   * — is rejected with A4P_DUPLICATE_ID, so the same order cannot reserve
   * quota twice under different execution ids.
   *
   * `now` is the evaluation instant in epoch milliseconds: one value used
   * for all time judgments of this call (validity window, period rollover,
   * record timestamps); a provided `now` must be a non-negative safe
   * integer. Defaults to the system clock; pass a fixed or offset value in
   * tests or to apply a caller-side clock-skew tolerance policy. The SDK
   * itself applies no tolerance — callers that want a window adjust the
   * value they pass.
   *
   * Failure semantics: a rejected reservation leaves the context unchanged
   * except when `now` is past `validUntil` — the status flips to EXPIRED
   * before the rejection is thrown, so a later call observing the context
   * sees the expiry. Callers relying on immutability of failed results
   * must copy.
   * @param context - The execution context to reserve against
   * @param execution - The execution to reserve
   * @param now - Optional evaluation instant in epoch milliseconds; defaults to Date.now()
   * @returns The updated execution context with the new reservation appended
   * @throws A4pError on validation, state, or quota-limit failure
   */
  export function createExecution(context: IntentExecutionContext, execution: Execution,
    now?: number): IntentExecutionContext;

  /**
   * Marks a pending execution completed. Allowed when the context is
   * ACTIVE, REVOKED, or EXPIRED (so pending executions reserved before
   * revocation or expiry can still be finalized). Auto-completion
   * (status → CONSUMED when every record is finalized and either the
   * count cap is reached or the total amount budget is exhausted) applies
   * only to an ACTIVE context: a REVOKED or EXPIRED context keeps its
   * status when its last pending record finalizes.
   * `now` is the evaluation instant in epoch milliseconds (see createExecution).
   * @param context - The execution context containing the pending execution
   * @param executionId - The ID of the execution to mark completed
   * @param now - Optional completion timestamp in epoch milliseconds; defaults to Date.now()
   * @returns The updated execution context
   * @throws A4pError on state failure
   */
  export function completeExecution(context: IntentExecutionContext, executionId: string,
    now?: number): IntentExecutionContext;

  /**
   * Cancels a pending execution and releases its quota: an explicit cancel
   * is a definitive outcome (the order will not happen), so the execution
   * count slot, amount, and period execution count roll back. Implicit paths
   * (timeouts, failures) do NOT roll back — that is the fail-closed rule
   * preventing over-commit when the downstream outcome is unknown. The
   * ledger itself stays append-only (`executions.length` never decreases).
   * `now` is the evaluation instant in epoch milliseconds (see createExecution).
   * Allowed when the context is ACTIVE, REVOKED, or EXPIRED (so pending
   * executions reserved before revocation or expiry can still be finalized).
   * @param context - The execution context containing the pending execution
   * @param executionId - The ID of the execution to cancel
   * @param now - Optional cancellation timestamp in epoch milliseconds; defaults to Date.now()
   * @returns The updated execution context with quota released
   * @throws A4pError on state failure
   */
  export function cancelExecution(context: IntentExecutionContext, executionId: string,
    now?: number): IntentExecutionContext;
}

/**
 * Canonical JSON codec for the intent levels and the execution context:
 * serialization produces the canonical form (see docs/canonical-json-spec.md),
 * deserialization parses and structurally validates the result, so a
 * malformed intent or context never passes through the codec.
 */
export declare namespace A4pCodec {
  /**
   * Serializes an IntentL1 to canonical JSON.
   * @param intentL1 - The Level 1 intent to serialize
   * @returns The canonical JSON string representation
   * @throws A4pError with A4P_SERIALIZE_FAIL on failure
   */
  export function serializeIntentL1(intentL1: IntentL1): string;

  /**
   * Deserializes an IntentL1 from canonical JSON, validating its structure.
   * @param serialized - The canonical JSON string to deserialize
   * @returns The deserialized and validated IntentL1
   * @throws A4pError with A4P_INVALID_PARAMETER (empty input / invalid
   *         structure) or A4P_DESERIALIZE_FAIL (invalid JSON)
   */
  export function deserializeIntentL1(serialized: string): IntentL1;

  /**
   * Serializes an IntentL2 to canonical JSON.
   * @param intentL2 - The Level 2 intent to serialize
   * @returns The canonical JSON string representation
   * @throws A4pError with A4P_SERIALIZE_FAIL on failure
   */
  export function serializeIntentL2(intentL2: IntentL2): string;

  /**
   * Deserializes an IntentL2 from canonical JSON, validating its structure.
   * @param serialized - The canonical JSON string to deserialize
   * @returns The deserialized and validated IntentL2
   * @throws A4pError with A4P_INVALID_PARAMETER (empty input / invalid
   *         structure) or A4P_DESERIALIZE_FAIL (invalid JSON)
   */
  export function deserializeIntentL2(serialized: string): IntentL2;

  /**
   * Serializes an IntentL3 to canonical JSON.
   * @param intentL3 - The Level 3 intent to serialize
   * @returns The canonical JSON string representation
   * @throws A4pError with A4P_SERIALIZE_FAIL on failure
   */
  export function serializeIntentL3(intentL3: IntentL3): string;

  /**
   * Deserializes an IntentL3 from canonical JSON, validating its structure.
   * @param serialized - The canonical JSON string to deserialize
   * @returns The deserialized and validated IntentL3
   * @throws A4pError with A4P_INVALID_PARAMETER (empty input / invalid
   *         structure) or A4P_DESERIALIZE_FAIL (invalid JSON)
   */
  export function deserializeIntentL3(serialized: string): IntentL3;

  /**
   * Serializes an IntentExecutionContext to canonical JSON.
   * @param context - The execution context to serialize
   * @returns The canonical JSON string representation
   * @throws A4pError with A4P_SERIALIZE_FAIL on failure
   */
  export function serializeIntentExecutionContext(context: IntentExecutionContext): string;

  /**
   * Deserializes an IntentExecutionContext from canonical JSON, validating
   * its structure: the authorized intent chain, the intentId derivation,
   * every ledger record, and the accounting snapshot. The authorization
   * snapshot is re-frozen on the way out, preserving the activation
   * invariant — execution checks read an immutable snapshot while the
   * ledger and the accounting figures stay mutable.
   * @param serialized - The canonical JSON string to deserialize
   * @returns The deserialized and validated IntentExecutionContext
   * @throws A4pError with A4P_INVALID_PARAMETER (empty input / invalid
   *         structure) or A4P_DESERIALIZE_FAIL (invalid JSON)
   */
  export function deserializeIntentExecutionContext(serialized: string): IntentExecutionContext;
}

