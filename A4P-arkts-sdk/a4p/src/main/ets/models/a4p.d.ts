/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

import { Signer } from '../api/signer';
import { SignatureVerifier } from '../api/verifier';

/**
 * Actions an intent can authorize.
 */
export enum IntentActionType {
  /** Agent payment: purchase of whitelisted products under payment constraints */
  AI_PAY = 0,
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
 * Signing information attached to each intent level: the algorithm and
 * key identifier used to sign this level, plus the signature itself.
 */
export interface SignatureInfo {
  /** Signing algorithm used; covered by the signature (see sign) */
  alg: AlgorithmType;
  /** Key index used for signing; covered by the signature (see sign) */
  keyIdentifier: string;
  /**
   * Signer's signature (base64url-encoded) over the canonical JSON of the
   * intent without this field (see docs/canonical-json-spec.md §4).
   * Because `alg`/`keyIdentifier` sit beside it inside `SignatureInfo`,
   * they are part of the signed bytes: any tampering with them changes
   * the signed content and invalidates this signature.
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
  /** Intent conversion configuration */
  intentConversion: string;
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
  /** Issuance timestamp (milliseconds since epoch); must be a non-negative integer */
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
  /** Signing timestamp (milliseconds since epoch); must be a non-negative integer */
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
   * three omitted
   */
  anchorTime?: number;
  /**
   * Period duration in seconds; provided together with anchorTime and
   * maxExecutionCountPerPeriod, or all three omitted
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
  /** Unique order identifier */
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
  /** Execution version; must match the intent's protocol version */
  version: string;
  /** Execution content (order details) */
  executionContent: Order;
}

/**
 * Status of an intent's overall execution lifecycle.
 */
export enum IntentExecutionStatus {
  /** Intent is active and can accept executions */
  ACTIVE,
  /** Intent execution is complete */
  COMPLETED,
  /** Intent has been revoked */
  REVOKED,
  /** Intent has expired */
  EXPIRED
}

/**
 * Status of an individual execution.
 */
export enum ExecutionStatus {
  /** Execution is pending */
  PENDING,
  /** Execution is complete */
  COMPLETED,
  /** Execution is canceled */
  CANCELED
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
export interface ExecutionInfo {
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
  /** Accounting snapshot derived from the constraints and the ledger (see ExecutionInfo and internal/accounting.ts) */
  executionInfo: ExecutionInfo;
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
  /** Unique seller identifier */
  id: string;
}

/**
 * SPU/SKU identifier pair that uniquely identifies a product variant.
 */
export interface SpuSkuInfo {
  /** Standard Product Unit identifier */
  spuId?: string;
  /** Stock Keeping Unit identifier */
  skuId?: string;
}

/**
 * A purchasable product identified by its seller and SPU/SKU pair.
 * Two products are considered equal when seller id, spuId, and skuId
 * all match (see productMatches in api/a4p.ts).
 */
export interface Product {
  /** Seller information */
  seller: SellerInfo;
  /** SPU/SKU information */
  spusku: SpuSkuInfo;
}

/**
 * One whitelist group of an AI_PAY intent's resource: the products the
 * user authorized for purchase plus the total quantity authorized for
 * the group.
 */
export interface ItemWhiteList {
  /** Authorized product whitelist; must not be empty */
  whiteList: Product[];
  /** Total quantity authorized for this group, consumed by non-canceled executions; must be a positive integer */
  quantity: number;
}

/**
 * Resource object of a structured intent: the whitelist groups that
 * together define the authorized purchase scope.
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
  /** original intent conversion */
  intentConversion: string;
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
  /** Verification result code (provider-specific) */
  code: string;
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
   * containing `alg`, `keyIdentifier`, and `sign`; all three are covered by
   * the signature because they sit in the top-level `sign` field which is
   * stripped only when computing signable bytes.
   *
   * `now` is the signing instant (epoch milliseconds) recorded in the
   * intent's `timestamp` field and thereby covered by the signature. Unlike
   * the evaluation instant of the execution APIs it drives no decision —
   * validity windows are judged at reservation time, not at signing time.
   * Defaults to the system clock; pass a fixed value in tests to make the
   * signed bytes reproducible.
   * @param request - The create-intent request containing intent details and constraints
   * @param agentSigner - The signer used to produce the agent's L1 signature
   * @param now - Optional signing instant in epoch milliseconds; defaults to Date.now()
   * @returns The signed Level 1 intent
   * @throws A4pError on validation or signing failure
   */
  function createIntent(request: CreateIntentRequest, agentSigner: Signer, now?: number): IntentL1;

  /**
   * Verifies the agent's L1 signature and issues a Level 2 intent signed by
   * the server. The embedded L1 is a deep copy.
   *
   * The `alg`/`keyIdentifier` used for verification come from the L1's own
   * `sign` field: they are read before verification, but because they are
   * part of the signed bytes, a successful verification retroactively
   * authenticates them — any tampering changes the signed content and
   * fails the signature check. (Verifiers must still bind alg to the key
   * type; see SignatureVerifier.)
   * `now` is the signing instant in epoch milliseconds (see createIntent).
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
   * intent. The embedded L2 is a deep copy. See issueIntent for how the
   * signed `alg`/`keyIdentifier` fields authenticate the verification choice.
   * `now` is the signing instant in epoch milliseconds (see createIntent).
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
   * issueIntent for how the signed `alg`/`keyIdentifier` fields authenticate
   * the verification choice.
   *
   * `now` is the activation instant in epoch milliseconds: it fixes the
   * period figures (`periodIndex`, `leftPeriodExecutionCount`) of the
   * initial `executionInfo` snapshot for periodic intents; non-periodic
   * intents derive no field from it. Defaults to the system clock; pass the
   * same value intended for the execution APIs to keep the activation
   * snapshot and the first reservation in the same period.
   * @param intentL3 - The Level 3 intent to verify and activate
   * @param userVerifier - The verifier used to check the user's L3 signature
   * @param now - Optional activation instant in epoch milliseconds; defaults to Date.now()
   * @returns The execution context with a frozen authorization snapshot
   * @throws A4pError on validation or verification failure
   */
  export function activateIntent(intentL3: IntentL3, userVerifier: SignatureVerifier,
    now?: number): IntentExecutionContext;

  /**
   * Verifies the user's L3 signature without creating an execution context.
   * The full chain is structurally validated before verification, exactly as
   * activateIntent does. See issueIntent for how the signed
   * `alg`/`keyIdentifier` fields authenticate the verification choice.
   *
   * Unlike the lifecycle APIs this is a pure query: the verifier's verdict
   * is the answer, so an invalid signature is returned as a
   * VerificationResult with `valid: false` (carrying the verifier's own
   * `code`/`message`) instead of being thrown. Structural problems
   * (malformed chain, unsupported version, serialization failure) and a
   * throwing verifier still throw.
   * @param intentL3 - The Level 3 intent whose signature is to be verified
   * @param userVerifier - The verifier used to check the user's L3 signature
   * @returns The verifier's result, including invalid (`valid: false`) verdicts
   * @throws A4pError on structural validation or serialization failure,
   *         or when the verifier itself throws
   */
  export function verifyIntentAuthenticity(intentL3: IntentL3, userVerifier: SignatureVerifier): VerificationResult;

  /**
   * Revokes the intent: no new executions may be reserved; pending ones
   * can still complete or cancel.
   * @param context - The execution context to revoke
   * @returns The updated execution context with status set to REVOKED
   * @throws A4pError when the intent is not active
   */
  export function revokeIntent(context: IntentExecutionContext): IntentExecutionContext;

  /**
   * `now` is the evaluation instant in epoch milliseconds: one value used
   * for all time judgments of this call (validity window, period rollover,
   * record timestamps). Defaults to the system clock; pass a fixed or
   * offset value in tests or to apply a caller-side clock-skew tolerance
   * policy. The SDK itself applies no tolerance — callers that want a
   * window adjust the value they pass.
   * @param context - The execution context to reserve against
   * @param execution - The execution to reserve
   * @param now - Optional evaluation instant in epoch milliseconds; defaults to Date.now()
   * @returns The updated execution context with the new reservation appended
   * @throws A4pError on validation, state, or quota-limit failure
   */
  export function createExecution(context: IntentExecutionContext, execution: Execution,
    now?: number): IntentExecutionContext;

  /**
   * Marks a pending execution completed. Failure semantics: a failed call
   * may still have mutated the context (an expired reserve flips status to
   * EXPIRED); callers relying on immutability of failed results must copy.
   * `now` is the evaluation instant in epoch milliseconds (see createExecution).
   * Allowed when the context is ACTIVE or REVOKED (so pending executions
   * reserved before revocation can still be finalized).
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
   * Allowed when the context is ACTIVE or REVOKED (so pending executions
   * reserved before revocation can still be finalized).
   * @param context - The execution context containing the pending execution
   * @param executionId - The ID of the execution to cancel
   * @param now - Optional cancellation timestamp in epoch milliseconds; defaults to Date.now()
   * @returns The updated execution context with quota released
   * @throws A4pError on state failure
   */
  export function cancelExecution(context: IntentExecutionContext, executionId: string,
    now?: number): IntentExecutionContext;
}

