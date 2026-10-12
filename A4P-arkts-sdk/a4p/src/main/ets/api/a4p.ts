/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

import { CreateIntentRequest, IntentIssueContext, VerificationResult, A4pErrorCode } from '../models/common';
import {
  IntentExecutionContext, IntentExecutionStatus, ExecutionStatus, ExecutionRecord, Execution
} from '../models/execution';
import { IntentActionType, IntentL1, IntentL2, IntentL3, SignatureInfo } from '../models/intent';
import { SigningEnv } from '../models/identity';
import { PaymentConstraints } from '../models/constraint';
import { ItemWhiteList, Product, CartItem, ShoppingItems } from '../models/product';
import { Signer } from './signer';
import { SignatureVerifier } from './verifier';
import { a4pThrow } from '../internal/result';
import { toSignableBytes, deepClone, deepFreeze } from '../internal/utils';
import {
  computeCurrentPeriodIndex, recomputeAccounting, computeExecutionInfo, computeExecutionCount,
  computePeriodExecutionCount, computeAccumulatedAmount, periodRuleOf, PeriodRule
} from '../internal/accounting';
import { A4pValidationFailure } from '../internal/validation';
import {
  productMatches, validateCreateIntentRequest, validateIssueContext, validateNow,
  validateSigningEnv, validateExecution, validateExecutionContext,
  validateIntentL1, validateIntentL2, validateIntentL3, validateSignatureInfo
} from '../internal/validation';
import { util } from '@kit.ArkTS';

const PROTOCOL_VERSION: string = '1.0';

/**
 * Throws the validation failure as an A4pError: the specific cause is
 * recorded via hilog, the thrown error carries the fixed message for
 * the code.
 * @param err - The validation failure to raise, or null to do nothing
 */
function raise(err: A4pValidationFailure | null): void {
  if (err) {
    a4pThrow(err.code, err.detail);
  }
}

/**
 * Runs a verifier call and maps any thrown exception to A4P_VERIFY_FAIL:
 * verifiers are expected to report failure through VerificationResult, but
 * a throwing verifier must still surface as an A4pError per the
 * throw-on-failure convention, never as a leaked raw exception. A verifier
 * returning no result at all is treated the same way. The verdict itself
 * is returned as-is, whatever it says.
 * @param verifier - The verifier to invoke
 * @param data - The signed bytes to verify
 * @param signature - The signature information to check
 * @param who - Whose signature is being checked (for the logged detail)
 * @returns The verifier's result
 * @throws A4pError with A4P_VERIFY_FAIL when the verifier throws or returns no result
 */
function runVerifier(verifier: SignatureVerifier, data: Uint8Array, signature: SignatureInfo,
                     who: string): VerificationResult {
  let result: VerificationResult;
  try {
    result = verifier.verify(data, signature);
  } catch (e) {
    a4pThrow(A4pErrorCode.A4P_VERIFY_FAIL, who + ' signature verification threw: ' + (e as Error).message);
  }
  // A verifier that returns no verdict at all is as broken as one that
  // throws: report it the same way instead of leaking a raw TypeError from
  // reading the absent result.
  if (!result) {
    a4pThrow(A4pErrorCode.A4P_VERIFY_FAIL, who + ' signature verifier returned no result');
  }
  return result;
}

/**
 * Runs a verifier and requires a valid verdict: the calling lifecycle API
 * must not endorse or activate an intent whose signature did not verify,
 * so an invalid result is mapped to A4P_VERIFY_FAIL. Use runVerifier
 * directly when the verdict itself is the answer (verifyIntentAuthenticity).
 * @param verifier - The verifier to invoke
 * @param data - The signed bytes to verify
 * @param signature - The signature information to check
 * @param who - Whose signature is being checked (for the logged detail)
 * @returns The verifier's result (a valid verdict)
 * @throws A4pError with A4P_VERIFY_FAIL when the verifier throws or reports an invalid signature
 */
function verifyOrThrow(verifier: SignatureVerifier, data: Uint8Array, signature: SignatureInfo,
                       who: string): VerificationResult {
  const result: VerificationResult = runVerifier(verifier, data, signature, who);
  if (!result.valid) {
    a4pThrow(A4pErrorCode.A4P_VERIFY_FAIL,
      who + ' signature verification failed: ' + (result.message ?? 'code ' + result.code));
  }
  return result;
}

/**
 * Runs a signer call and validates its result: a throwing signer is
 * mapped to A4P_SIGNING_FAIL, and so is one that returns no SignatureInfo
 * at all or a malformed one. The signer is user-supplied code — its
 * return value is a boundary input, exactly like a verifier's — and an
 * intent signed with a malformed result would only fail at the far end
 * of the chain (every downstream validation rejects it), so the failure
 * is surfaced here instead. Mirrors runVerifier's treatment of a
 * verifier's return value.
 * @param signer - The signer to invoke
 * @param data - The bytes to sign
 * @param who - What is being signed (for the logged detail)
 * @returns The signer's SignatureInfo (present and well-formed)
 * @throws A4pError with A4P_SIGNING_FAIL when the signer throws or returns no result or a malformed one
 */
function runSigner(signer: Signer, data: Uint8Array, who: string): SignatureInfo {
  let info: SignatureInfo;
  try {
    info = signer.sign(data);
  } catch (e) {
    a4pThrow(A4pErrorCode.A4P_SIGNING_FAIL, 'Failed to sign ' + who + ': ' + (e as Error).message);
  }
  const err: A4pValidationFailure | null = validateSignatureInfo(info, who);
  if (err) {
    a4pThrow(A4pErrorCode.A4P_SIGNING_FAIL, who + ' signer returned an invalid result: ' + err.detail);
  }
  return info;
}

/**
 * Core A4P protocol operations: the intent lifecycle (createIntent,
 * issueIntent, signIntent, activateIntent), execution accounting
 * (createExecution, completeExecution, cancelExecution), revocation,
 * and authenticity verification. All functions return their data
 * directly and throw A4pError on any failure.
 */
export namespace A4p {

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
   * @throws A4pError on validation, serialization, or signing failure
   */
  export function createIntent(request: CreateIntentRequest, agentSigner: Signer, now?: number): IntentL1 {
    if (now !== undefined) {
      raise(validateNow(now));
    }
    raise(validateCreateIntentRequest(request));

    const intentId: string = request.intentId ?? util.generateRandomUUID();
    const timestamp: number = now !== undefined ? now : Date.now();

    const intentL1: IntentL1 = {
      version: PROTOCOL_VERSION,
      intentId: intentId,
      intentConversation: request.intentConversation,
      intentSummary: request.intentSummary,
      structuredIntent: deepClone(request.structuredIntent),
      timestamp: timestamp,
    };

    const dataToSign: Uint8Array = toSignableBytes(intentL1);
    intentL1.sign = runSigner(agentSigner, dataToSign, 'IntentL1');

    return intentL1;
  }

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
   * @throws A4pError on validation, serialization, or verification failure
   */
  export function issueIntent(intentL1: IntentL1, agentVerifier: SignatureVerifier, issueContext: IntentIssueContext,
                              serverSigner: Signer, now?: number): IntentL2 {
    if (now !== undefined) {
      raise(validateNow(now));
    }
    raise(validateIssueContext(issueContext));
    raise(validateIntentL1(intentL1));
    if (intentL1.version != PROTOCOL_VERSION) {
      a4pThrow(A4pErrorCode.A4P_UNSUPPORTED_VERSION, 'IntentL1 version ' + intentL1.version + ' is not supported');
    }

    // The issuance stamp must not predate the intent it endorses: an L2
    // stamped before its L1 violates the chain's monotonic timestamps and
    // would be refused by every later step, so reject the skewed clock here,
    // with the precise cause, instead of signing a dead-end L2. This covers
    // the default clock too (a wall clock stepped back past the L1 stamp).
    const timestamp: number = now !== undefined ? now : Date.now();
    if (timestamp < intentL1.timestamp) {
      a4pThrow(A4pErrorCode.A4P_INVALID_PARAMETER,
        'Issuance timestamp ' + timestamp + ' is earlier than intentL1.timestamp ' + intentL1.timestamp);
    }

    const l1Data: Uint8Array = toSignableBytes(intentL1);
    // validateIntentL1 guarantees sign is present (validateSignatureInfo
    // rejects a missing or blank one); the cast only satisfies the compiler.
    verifyOrThrow(agentVerifier, l1Data, intentL1.sign as SignatureInfo, 'Agent');

    const intentL2: IntentL2 = {
      version: PROTOCOL_VERSION,
      intentL1: deepClone(intentL1),
      userIdentity: deepClone(issueContext.userIdentity),
      agentIdentity: deepClone(issueContext.agentIdentity),
      serviceProviderIdentity: deepClone(issueContext.serviceProviderIdentity),
      timestamp: timestamp,
    };

    const dataToSign: Uint8Array = toSignableBytes(intentL2);
    intentL2.sign = runSigner(serverSigner, dataToSign, 'IntentL2');

    return intentL2;
  }

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
   * @throws A4pError on validation, serialization, or verification failure
   */
  export function signIntent(intentL2: IntentL2, serverVerifier: SignatureVerifier, signingEnv: SigningEnv,
                             userSigner: Signer, now?: number): IntentL3 {
    if (now !== undefined) {
      raise(validateNow(now));
    }
    raise(validateSigningEnv(signingEnv));
    raise(validateIntentL2(intentL2));
    if (intentL2.version != PROTOCOL_VERSION) {
      a4pThrow(A4pErrorCode.A4P_UNSUPPORTED_VERSION, 'IntentL2 version ' + intentL2.version + ' is not supported');
    }

    // The user's signing stamp must not predate the issuance it authorizes
    // (see issueIntent): reject the skewed clock here instead of signing a
    // dead-end L3 that activation would always refuse.
    const timestamp: number = now !== undefined ? now : Date.now();
    if (timestamp < intentL2.timestamp) {
      a4pThrow(A4pErrorCode.A4P_INVALID_PARAMETER,
        'Signing timestamp ' + timestamp + ' is earlier than intentL2.timestamp ' + intentL2.timestamp);
    }

    const l2Data: Uint8Array = toSignableBytes(intentL2);
    // validateIntentL2 guarantees sign is present (validateSignatureInfo
    // rejects a missing or blank one); the cast only satisfies the compiler.
    verifyOrThrow(serverVerifier, l2Data, intentL2.sign as SignatureInfo, 'Server');

    const intentL3: IntentL3 = {
      version: PROTOCOL_VERSION,
      intentL2: deepClone(intentL2),
      signingEnv: deepClone(signingEnv),
      timestamp: timestamp,
    };

    const dataToSign: Uint8Array = toSignableBytes(intentL3);
    intentL3.sign = runSigner(userSigner, dataToSign, 'IntentL3');

    return intentL3;
  }

  /**
   * Checks that the intent chain carries the protocol version at every
   * level. A mixed-version chain is signature-valid but semantically
   * unsupported and must be rejected.
   * @param intentL3 - The Level 3 intent chain to check
   * @returns A validation failure if any version mismatches, or null if all match
   */
  function checkChainVersions(intentL3: IntentL3): A4pValidationFailure | null {
    if (intentL3.version != PROTOCOL_VERSION) {
      return new A4pValidationFailure(A4pErrorCode.A4P_UNSUPPORTED_VERSION,
        'IntentL3 version ' + intentL3.version + ' is not supported');
    }
    if (intentL3.intentL2.version != PROTOCOL_VERSION) {
      return new A4pValidationFailure(A4pErrorCode.A4P_UNSUPPORTED_VERSION,
        'IntentL2 version ' + intentL3.intentL2.version + ' is not supported');
    }
    if (intentL3.intentL2.intentL1.version != PROTOCOL_VERSION) {
      return new A4pValidationFailure(A4pErrorCode.A4P_UNSUPPORTED_VERSION,
        'IntentL1 version ' + intentL3.intentL2.intentL1.version + ' is not supported');
    }
    return null;
  }

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
   * @throws A4pError on validation, serialization, or verification failure
   */
  export function activateIntent(intentL3: IntentL3, userVerifier: SignatureVerifier,
                                 now?: number): IntentExecutionContext {
    if (now !== undefined) {
      raise(validateNow(now));
    }
    raise(validateIntentL3(intentL3));
    raise(checkChainVersions(intentL3));

    // The activation instant must not predate the authorization it
    // activates (the monotonic rule issueIntent/signIntent enforce for
    // their stamps): a context created before the L3's signing timestamp
    // would cover executions at instants the user had not signed at. This
    // covers the default clock too (a wall clock stepped back past the L3
    // stamp).
    const nowMs: number = now !== undefined ? now : Date.now();
    if (nowMs < intentL3.timestamp) {
      a4pThrow(A4pErrorCode.A4P_INVALID_PARAMETER,
        'Activation timestamp ' + nowMs + ' is earlier than intentL3.timestamp ' + intentL3.timestamp);
    }

    const l3Data: Uint8Array = toSignableBytes(intentL3);
    // validateIntentL3 guarantees sign is present (validateSignatureInfo
    // rejects a missing or blank one); the cast only satisfies the compiler.
    verifyOrThrow(userVerifier, l3Data, intentL3.sign as SignatureInfo, 'User');

    const intentId: string = intentL3.intentL2.intentL1.intentId;
    const constraints: PaymentConstraints = intentL3.intentL2.intentL1.structuredIntent.constraints;

    const authorizedIntent: IntentL3 = deepFreeze(deepClone(intentL3));

    const context: IntentExecutionContext = {
      version: PROTOCOL_VERSION,
      intentId: intentId,
      authorizedIntent: authorizedIntent,
      status: IntentExecutionStatus.ACTIVE,
      executions: [],
      executionInfo: computeExecutionInfo(constraints, [], nowMs),
    };

    return context;
  }

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
  export function verifyIntentAuthenticity(intentL3: IntentL3, serverVerifier: SignatureVerifier): VerificationResult {
    // The L3 is only the carrier here: its own (user) signature is
    // activateIntent's to verify, not this query's — the sole L3-level
    // requirement is that it contains the L2 whose signature is checked.
    if (!intentL3 || !intentL3.intentL2) {
      a4pThrow(A4pErrorCode.A4P_INVALID_PARAMETER, 'intentL3 must contain the intentL2 to verify');
    }

    const intentL2: IntentL2 = intentL3.intentL2;
    raise(validateIntentL2(intentL2));
    // The verified chain is the L2 with its embedded L1: a mixed-version
    // chain is signature-valid but semantically unsupported. The carrier
    // L3's version plays no role in the verdict.
    if (intentL2.version != PROTOCOL_VERSION) {
      a4pThrow(A4pErrorCode.A4P_UNSUPPORTED_VERSION, 'IntentL2 version ' + intentL2.version + ' is not supported');
    }
    if (intentL2.intentL1.version != PROTOCOL_VERSION) {
      a4pThrow(A4pErrorCode.A4P_UNSUPPORTED_VERSION,
        'IntentL1 version ' + intentL2.intentL1.version + ' is not supported');
    }

    const l2Data: Uint8Array = toSignableBytes(intentL2);
    // validateIntentL2 guarantees sign is present (validateSignatureInfo
    // rejects a missing or blank one); the cast only satisfies the compiler.
    return runVerifier(serverVerifier, l2Data, intentL2.sign as SignatureInfo, 'Server');
  }

  /**
   * Revokes the intent: no new executions may be reserved; pending ones
   * can still complete or cancel.
   * @param context - The execution context to revoke
   * @returns The updated execution context with status set to REVOKED
   * @throws A4pError when the intent is not active
   */
  export function revokeIntent(context: IntentExecutionContext): IntentExecutionContext {
    raise(validateExecutionContext(context));
    if (context.status !== IntentExecutionStatus.ACTIVE) {
      a4pThrow(A4pErrorCode.A4P_INVALID_STATUS,
        'revokeIntent requires an active intent, current status: ' + context.status);
    }

    context.status = IntentExecutionStatus.REVOKED;
    return context;
  }

  /**
   * Enforces the authorization scope the user actually signed: every cart
   * product must be on at least one whitelist group of the resource.
   * @param context - The execution context carrying the authorized intent
   * @param cartItems - The ordered cart items to validate against the authorized scope
   * @returns A validation failure if the cart exceeds scope, or null if within scope
   */
  function enforceWhitelistScope(context: IntentExecutionContext, cartItems: CartItem[]): A4pValidationFailure | null {
    const resource: ShoppingItems = context.authorizedIntent.intentL2.intentL1.structuredIntent.resource;
    for (let i = 0; i < cartItems.length; i++) {
      const item = cartItems[i];
      const whitelisted: boolean = resource.itemsWhiteList.some(
        (group: ItemWhiteList) => group.whiteList.some((p: Product) => productMatches(p, item.product)));
      if (!whitelisted) {
        return new A4pValidationFailure(A4pErrorCode.A4P_PRODUCT_NOT_AUTHORIZED,
          'Cart product is not in the authorized whitelist: seller ' + item.product.seller.id +
          ', sku ' + item.product.spusku.skuId);
      }
    }
    return null;
  }

  /**
   * Enforces the order template the user actually signed. The resource is
   * a template, not a budget: every AI_PAY execution's cart must carry
   * exactly one cart item per whitelist group — same length, arrays in
   * any order — with the item's product drawn from its group's whitelist
   * (OR within a group, AND across groups) and its quantity exactly the
   * group's quantity. Cumulative bounds across executions come from the
   * intent's constraints (amount, count, period caps), not from the group
   * quantities.
   *
   * Intent validation guarantees a product uniquely determines its group
   * (no product in two groups, none twice within one group), so the first
   * group matching a cart item is its group; a second cart item landing
   * on an already-matched group, a shorter or longer cart, a quantity
   * other than the group's, or an uncovered group all fail as
   * A4P_CART_MISMATCH.
   * @param context - The execution context carrying the authorized intent
   * @param cartItems - The ordered cart items to validate against the order template
   * @returns A validation failure if the cart does not match the template, or null if it does
   */
  function enforceTemplateMatch(context: IntentExecutionContext, cartItems: CartItem[]): A4pValidationFailure | null {
    const groups: ItemWhiteList[] =
      context.authorizedIntent.intentL2.intentL1.structuredIntent.resource.itemsWhiteList;
    if (cartItems.length !== groups.length) {
      return new A4pValidationFailure(A4pErrorCode.A4P_CART_MISMATCH,
        'Cart has ' + cartItems.length + ' items but the authorized resource has ' + groups.length +
        ' whitelist groups');
    }
    const matchedGroups: Set<number> = new Set<number>();
    for (let i = 0; i < cartItems.length; i++) {
      const item: CartItem = cartItems[i];
      let groupIndex: number = -1;
      for (let g = 0; g < groups.length; g++) {
        if (groups[g].whiteList.some((p: Product) => productMatches(p, item.product))) {
          groupIndex = g;
          break;
        }
      }
      if (groupIndex < 0) {
        // Unreachable after enforceWhitelistScope (scope runs first);
        // kept fail-closed for standalone use.
        return new A4pValidationFailure(A4pErrorCode.A4P_PRODUCT_NOT_AUTHORIZED,
          'Cart product is not in the authorized whitelist: seller ' + item.product.seller.id +
          ', sku ' + item.product.spusku.skuId);
      }
      if (matchedGroups.has(groupIndex)) {
        return new A4pValidationFailure(A4pErrorCode.A4P_CART_MISMATCH,
          'Two cart items match whitelist group ' + groupIndex + ': the group allows a single item per execution');
      }
      matchedGroups.add(groupIndex);
      if (item.quantity !== groups[groupIndex].quantity) {
        return new A4pValidationFailure(A4pErrorCode.A4P_CART_MISMATCH,
          'Cart item quantity ' + item.quantity + ' does not equal the authorized quantity ' +
          groups[groupIndex].quantity + ' of whitelist group ' + groupIndex);
      }
    }
    for (let g = 0; g < groups.length; g++) {
      if (!matchedGroups.has(g)) {
        return new A4pValidationFailure(A4pErrorCode.A4P_CART_MISMATCH,
          'Whitelist group ' + g + ' is not covered by any cart item');
      }
    }
    return null;
  }

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
   * must copy. Checks run in a fixed order — request structure, parameter
   * consistency with the frozen snapshot (action, currency),
   * context status, validity window (the lazy EXPIRED flip), duplicate
   * ids, then quota — so parameter-class rejections never depend on the
   * ledger state and the expiry flip is not skipped by duplicate rejections.
   * @param context - The execution context to reserve against
   * @param execution - The execution to reserve
   * @param now - Optional evaluation instant in epoch milliseconds; defaults to Date.now()
   * @returns The updated execution context with the new reservation appended
   * @throws A4pError on validation, state, or quota-limit failure
   */
  export function createExecution(context: IntentExecutionContext, execution: Execution,
                                  now?: number): IntentExecutionContext {
    raise(validateExecutionContext(context));
    if (now !== undefined) {
      raise(validateNow(now));
    }
    raise(validateExecution(execution));
    // The caller's object is never mutated: the id is resolved locally
    // (generated when absent) and assigned to the ledger's own deep copy
    // only after every check has passed.
    const executionId: string = execution.id !== undefined ? execution.id : util.generateRandomUUID();

    // Checks run in a fixed order — request structure (above), parameter
    // consistency with the frozen snapshot (action, currency),
    // context status, validity window (the lazy EXPIRED flip), duplicate
    // ids, then quota — so parameter-class rejections never depend on the
    // ledger state, and an expired context flips before duplicate-id
    // rejections are reported.
    const l1: IntentL1 = context.authorizedIntent.intentL2.intentL1;
    const constraints: PaymentConstraints = l1.structuredIntent.constraints;
    const nowMs: number = now !== undefined ? now : Date.now();

    if (execution.action !== l1.structuredIntent.action) {
      a4pThrow(A4pErrorCode.A4P_INVALID_PARAMETER,
        'Execution action ' + execution.action + ' does not match intent action ' + l1.structuredIntent.action);
    }
    if (execution.executionContent.currency !== constraints.currency) {
      a4pThrow(A4pErrorCode.A4P_INVALID_PARAMETER,
        'Currency mismatch: order is ' + execution.executionContent.currency +
        ', intent constraint is ' + constraints.currency);
    }

    if (context.status !== IntentExecutionStatus.ACTIVE) {
      a4pThrow(A4pErrorCode.A4P_INVALID_STATUS,
        'createExecution requires an active intent, current status: ' + context.status);
    }

    if (nowMs < constraints.validFrom) {
      a4pThrow(A4pErrorCode.A4P_TIMESTAMP_TOO_EARLY,
        'Current time ' + nowMs + ' is before validFrom ' + constraints.validFrom);
    }
    if (nowMs > constraints.validUntil) {
      context.status = IntentExecutionStatus.EXPIRED;
      a4pThrow(A4pErrorCode.A4P_TIMESTAMP_EXPIRED,
        'Current time ' + nowMs + ' is after validUntil ' + constraints.validUntil);
    }

    const duplicateExists: boolean = context.executions.some(
      (r: ExecutionRecord) => r.executionId === executionId);
    if (duplicateExists) {
      a4pThrow(A4pErrorCode.A4P_DUPLICATE_ID, 'Execution ID already exists: ' + executionId);
    }
    // orderId is the payment idempotency key: one order reserves quota at
    // most once per context — canceled records included, so a canceled
    // reservation cannot be re-reserved under a fresh execution id.
    const duplicateOrder: boolean = context.executions.some(
      (r: ExecutionRecord) => r.execution.executionContent.orderId === execution.executionContent.orderId);
    if (duplicateOrder) {
      a4pThrow(A4pErrorCode.A4P_DUPLICATE_ID,
        'Order ID already exists: ' + execution.executionContent.orderId);
    }
    // Optional caps: an absent cap means no limit of that kind, so the
    // check is skipped entirely.
    const countCap: number | undefined = constraints.maxExecuteCount;
    if (countCap !== undefined) {
      const executionCount: number = computeExecutionCount(context.executions);
      if (executionCount >= countCap) {
        a4pThrow(A4pErrorCode.A4P_EXECUTION_COUNT_EXCEEDED,
          'Execution count ' + executionCount + ' reached maxExecuteCount ' + countCap);
      }
    }

    const orderAmount: number = execution.executionContent.amount;
    const singleOrderCap: number | undefined = constraints.singleOrderMaxAmount;
    if (singleOrderCap !== undefined && orderAmount > singleOrderCap) {
      a4pThrow(A4pErrorCode.A4P_SINGLE_ORDER_AMOUNT_EXCEEDED,
        'Order amount ' + orderAmount + ' exceeds singleOrderMaxAmount ' + singleOrderCap);
    }
    const accumulatedAmount: number = computeAccumulatedAmount(context.executions);
    if (accumulatedAmount + orderAmount > constraints.totalMaxAmount) {
      a4pThrow(A4pErrorCode.A4P_AMOUNT_EXCEEDED,
        'Total amount ' + (accumulatedAmount + orderAmount) + ' exceeds totalMaxAmount ' + constraints.totalMaxAmount);
    }

    if (l1.structuredIntent.action === IntentActionType.AI_PAY) {
      raise(enforceWhitelistScope(context, execution.executionContent.cartItems));
      raise(enforceTemplateMatch(context, execution.executionContent.cartItems));
    }

    // The period rule is all-or-nothing: periodic intents get a period
    // index on their records and a per-period cap; non-periodic intents
    // have neither.
    const periodRule: PeriodRule | null = periodRuleOf(constraints);
    let currentPeriodIndex: number | undefined = undefined;
    if (periodRule !== null) {
      currentPeriodIndex = computeCurrentPeriodIndex(periodRule, nowMs);
      const periodExecutionCount: number = computePeriodExecutionCount(context.executions, currentPeriodIndex);
      if (periodExecutionCount >= periodRule.maxExecutionCountPerPeriod) {
        a4pThrow(A4pErrorCode.A4P_PERIOD_EXECUTION_COUNT_EXCEEDED,
          'Period ' + currentPeriodIndex + ' execution count ' + periodExecutionCount +
          ' reached maxExecutionCountPerPeriod ' + periodRule.maxExecutionCountPerPeriod);
      }
    }

    const storedExecution: Execution = deepClone(execution);
    storedExecution.id = executionId;
    const record: ExecutionRecord = {
      executionId: executionId,
      status: ExecutionStatus.PENDING,
      reservedAt: nowMs,
      execution: storedExecution,
    };
    if (currentPeriodIndex !== undefined) {
      record.periodIndex = currentPeriodIndex;
    }

    context.executions.push(record);
    recomputeAccounting(context, nowMs);

    return context;
  }

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
                                    now?: number): IntentExecutionContext {
    raise(validateExecutionContext(context));
    if (now !== undefined) {
      raise(validateNow(now));
    }
    if (context.status !== IntentExecutionStatus.ACTIVE && context.status !== IntentExecutionStatus.REVOKED
      && context.status !== IntentExecutionStatus.EXPIRED) {
      a4pThrow(A4pErrorCode.A4P_INVALID_STATUS,
        'completeExecution requires an active, revoked, or expired intent, current status: ' + context.status);
    }

    const record: ExecutionRecord | undefined = context.executions.find(
      (r: ExecutionRecord) => r.executionId === executionId);
    if (!record) {
      a4pThrow(A4pErrorCode.A4P_INVALID_PARAMETER, 'Execution not found: ' + executionId);
    }
    if (record.status !== ExecutionStatus.PENDING) {
      a4pThrow(A4pErrorCode.A4P_INVALID_STATUS, 'Execution is not pending, current status: ' + record.status);
    }

    record.status = ExecutionStatus.COMPLETED;
    record.completedAt = now !== undefined ? now : Date.now();

    const allDone: boolean = context.executions.every(
      (r: ExecutionRecord) => r.status === ExecutionStatus.COMPLETED || r.status === ExecutionStatus.CANCELED);
    // Auto-complete requires an ACTIVE context, a drained budget, and a
    // settled ledger: the count cap is reached or the total amount budget
    // is exhausted, and every record is finalized (flipping while a record
    // is still PENDING would strand it — finalize APIs require a
    // non-CONSUMED status). Without maxExecuteCount the count trigger is
    // absent but the amount trigger remains (totalMaxAmount is always
    // present). A REVOKED or EXPIRED context keeps its status —
    // revocation/expiry is a fact about the intent's history, not erased
    // by the ledger draining.
    const constraints: PaymentConstraints =
      context.authorizedIntent.intentL2.intentL1.structuredIntent.constraints;
    const countCap: number | undefined = constraints.maxExecuteCount;
    const countReached: boolean = countCap !== undefined &&
      computeExecutionCount(context.executions) >= countCap;
    const amountExhausted: boolean = computeAccumulatedAmount(context.executions) >= constraints.totalMaxAmount;
    if (context.status === IntentExecutionStatus.ACTIVE && allDone && (countReached || amountExhausted)) {
      context.status = IntentExecutionStatus.CONSUMED;
    }

    return context;
  }

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
                                  now?: number): IntentExecutionContext {
    raise(validateExecutionContext(context));
    if (now !== undefined) {
      raise(validateNow(now));
    }
    if (context.status !== IntentExecutionStatus.ACTIVE && context.status !== IntentExecutionStatus.REVOKED
      && context.status !== IntentExecutionStatus.EXPIRED) {
      a4pThrow(A4pErrorCode.A4P_INVALID_STATUS,
        'cancelExecution requires an active, revoked, or expired intent, current status: ' + context.status);
    }

    const record: ExecutionRecord | undefined = context.executions.find(
      (r: ExecutionRecord) => r.executionId === executionId);
    if (!record) {
      a4pThrow(A4pErrorCode.A4P_INVALID_PARAMETER, 'Execution not found: ' + executionId);
    }
    if (record.status !== ExecutionStatus.PENDING) {
      a4pThrow(A4pErrorCode.A4P_INVALID_STATUS, 'Execution is not pending, current status: ' + record.status);
    }

    record.status = ExecutionStatus.CANCELED;
    const nowMs: number = now !== undefined ? now : Date.now();
    record.canceledAt = nowMs;

    // Quota release happens by re-deriving from the ledger — the canceled
    // record drops out of the execution count, accumulated amount, and
    // current-period counts, while earlier-period records are excluded by
    // their periodIndex.
    recomputeAccounting(context, nowMs);

    return context;
  }
}
