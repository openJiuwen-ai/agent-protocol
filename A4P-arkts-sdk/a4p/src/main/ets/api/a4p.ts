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
  validateCreateIntentRequest, validateIssueContext,
  validateSigningEnv, validateExecution, validateIntentL1, validateIntentL2, validateIntentL3
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
 * throw-on-failure convention, never as a leaked raw exception. The
 * verdict itself is returned as-is, whatever it says.
 * @param verifier - The verifier to invoke
 * @param data - The signed bytes to verify
 * @param signature - The signature information to check
 * @param who - Whose signature is being checked (for the logged detail)
 * @returns The verifier's result
 * @throws A4pError with A4P_VERIFY_FAIL when the verifier throws
 */
function runVerifier(verifier: SignatureVerifier, data: Uint8Array,
                     signature: SignatureInfo, who: string): VerificationResult {
  let result: VerificationResult;
  try {
    result = verifier.verify(data, signature);
  } catch (e) {
    a4pThrow(A4pErrorCode.A4P_VERIFY_FAIL, who + ' signature verification threw: ' + (e as Error).message);
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
function verifyOrThrow(verifier: SignatureVerifier, data: Uint8Array,
                       signature: SignatureInfo, who: string): VerificationResult {
  const result: VerificationResult = runVerifier(verifier, data, signature, who);
  if (!result.valid) {
    a4pThrow(A4pErrorCode.A4P_VERIFY_FAIL,
      who + ' signature verification failed: ' + (result.message ?? 'code ' + result.code));
  }
  return result;
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
   * @throws A4pError on validation, serialization, or signing failure
   */
  export function createIntent(request: CreateIntentRequest, agentSigner: Signer, now?: number): IntentL1 {
    raise(validateCreateIntentRequest(request));

    const intentId: string = request.intentId ?? util.generateRandomUUID();
    const timestamp: number = now !== undefined ? now : Date.now();

    const intentL1: IntentL1 = {
      version: PROTOCOL_VERSION,
      intentId: intentId,
      intentConversion: request.intentConversion,
      intentSummary: request.intentSummary,
      structuredIntent: deepClone(request.structuredIntent),
      timestamp: timestamp,
    };

    const dataToSign: Uint8Array = toSignableBytes(intentL1);
    try {
      intentL1.sign = agentSigner.sign(dataToSign);
    } catch (e) {
      a4pThrow(A4pErrorCode.A4P_SIGNING_FAIL, 'Failed to sign IntentL1: ' + (e as Error).message);
    }

    return intentL1;
  }

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
   * @param intentL1 - The Level 1 intent to verify and endorse
   * @param agentVerifier - The verifier used to check the agent's L1 signature
   * `now` is the signing instant in epoch milliseconds (see createIntent).
   * @param issueContext - The issue context carrying user, agent, and service-provider identities
   * @param serverSigner - The signer used to produce the server's L2 signature
   * @param now - Optional signing instant in epoch milliseconds; defaults to Date.now()
   * @returns The signed Level 2 intent with the embedded L1
   * @throws A4pError on validation, serialization, or verification failure
   */
  export function issueIntent(intentL1: IntentL1, agentVerifier: SignatureVerifier,
                              issueContext: IntentIssueContext, serverSigner: Signer,
                              now?: number): IntentL2 {
    raise(validateIssueContext(issueContext));
    raise(validateIntentL1(intentL1));
    if (intentL1.version != PROTOCOL_VERSION) {
      a4pThrow(A4pErrorCode.A4P_UNSUPPORTED_VERSION, 'IntentL1 version ' + intentL1.version + ' is not supported');
    }

    if (!intentL1.sign) {
      a4pThrow(A4pErrorCode.A4P_INVALID_PARAMETER, 'IntentL1 has no signature');
    }
    const l1Data: Uint8Array = toSignableBytes(intentL1);
    verifyOrThrow(agentVerifier, l1Data, intentL1.sign, 'Agent');

    const timestamp: number = now !== undefined ? now : Date.now();
    const intentL2: IntentL2 = {
      version: PROTOCOL_VERSION,
      intentL1: deepClone(intentL1),
      userIdentity: issueContext.userIdentity,
      agentIdentity: issueContext.agentIdentity,
      serviceProviderIdentity: issueContext.serviceProviderIdentity,
      timestamp: timestamp,
    };

    const dataToSign: Uint8Array = toSignableBytes(intentL2);
    try {
      intentL2.sign = serverSigner.sign(dataToSign);
    } catch (e) {
      a4pThrow(A4pErrorCode.A4P_SIGNING_FAIL, 'Failed to sign IntentL2: ' + (e as Error).message);
    }

    return intentL2;
  }

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
   * @throws A4pError on validation, serialization, or verification failure
   */
  export function signIntent(intentL2: IntentL2, serverVerifier: SignatureVerifier,
                             signingEnv: SigningEnv, userSigner: Signer,
                             now?: number): IntentL3 {
    raise(validateSigningEnv(signingEnv));
    raise(validateIntentL2(intentL2));
    if (intentL2.version != PROTOCOL_VERSION) {
      a4pThrow(A4pErrorCode.A4P_UNSUPPORTED_VERSION, 'IntentL2 version ' + intentL2.version + ' is not supported');
    }

    if (!intentL2.sign) {
      a4pThrow(A4pErrorCode.A4P_INVALID_PARAMETER, 'IntentL2 has no signature');
    }
    const l2Data: Uint8Array = toSignableBytes(intentL2);
    verifyOrThrow(serverVerifier, l2Data, intentL2.sign, 'Server');

    const timestamp: number = now !== undefined ? now : Date.now();
    const intentL3: IntentL3 = {
      version: PROTOCOL_VERSION,
      intentL2: deepClone(intentL2),
      signingEnv: signingEnv,
      timestamp: timestamp,
    };

    const dataToSign: Uint8Array = toSignableBytes(intentL3);
    try {
      intentL3.sign = userSigner.sign(dataToSign);
    } catch (e) {
      a4pThrow(A4pErrorCode.A4P_SIGNING_FAIL, 'Failed to sign IntentL3: ' + (e as Error).message);
    }

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
   * @throws A4pError on validation, serialization, or verification failure
   */
  export function activateIntent(intentL3: IntentL3, userVerifier: SignatureVerifier,
                                 now?: number): IntentExecutionContext {
    raise(validateIntentL3(intentL3));
    raise(checkChainVersions(intentL3));

    if (!intentL3.sign) {
      a4pThrow(A4pErrorCode.A4P_INVALID_PARAMETER, 'IntentL3 has no signature');
    }

    const l3Data: Uint8Array = toSignableBytes(intentL3);
    verifyOrThrow(userVerifier, l3Data, intentL3.sign, 'User');

    const intentId: string = intentL3.intentL2.intentL1.intentId;
    const constraints: PaymentConstraints = intentL3.intentL2.intentL1.structuredIntent.constraints;
    const nowMs: number = now !== undefined ? now : Date.now();

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
  export function verifyIntentAuthenticity(intentL3: IntentL3, userVerifier: SignatureVerifier): VerificationResult {
    raise(validateIntentL3(intentL3));
    raise(checkChainVersions(intentL3));

    if (!intentL3.sign) {
      a4pThrow(A4pErrorCode.A4P_INVALID_PARAMETER, 'IntentL3 has no signature');
    }

    const l3Data: Uint8Array = toSignableBytes(intentL3);
    return runVerifier(userVerifier, l3Data, intentL3.sign, 'User');
  }

  /**
   * Revokes the intent: no new executions may be reserved; pending ones
   * can still complete or cancel.
   * @param context - The execution context to revoke
   * @returns The updated execution context with status set to REVOKED
   * @throws A4pError when the intent is not active
   */
  export function revokeIntent(context: IntentExecutionContext): IntentExecutionContext {
    if (context.status !== IntentExecutionStatus.ACTIVE) {
      a4pThrow(A4pErrorCode.A4P_INVALID_STATUS,
        'revokeIntent requires an active intent, current status: ' + context.status);
    }

    context.status = IntentExecutionStatus.REVOKED;
    return context;
  }

  /**
   * Checks whether two products refer to the same item by comparing
   * seller ID, SPU ID, and SKU ID.
   * @param a - First product
   * @param b - Second product
   * @returns True if both products match on seller, SPU, and SKU
   */
  function productMatches(a: Product, b: Product): boolean {
    return a.seller.id === b.seller.id && a.spusku.spuId === b.spusku.spuId && a.spusku.skuId === b.spusku.skuId;
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
        return new A4pValidationFailure(A4pErrorCode.A4P_PRODUCT_QUANTITY_EXCEEDED,
          'Cart product is not in the authorized whitelist: seller ' + item.product.seller.id +
          ', sku ' + item.product.spusku.skuId);
      }
    }
    return null;
  }

  /**
   * Total quantity of cart items whose product belongs to the given
   * whitelist group. An item whose product appears in several groups counts
   * against every matching group (fail-closed).
   * @param cartItems - The cart items to sum quantities for
   * @param group - The whitelist group to match products against
   * @returns The total quantity of items matching the group
   */
  function cartQuantityInGroup(cartItems: CartItem[], group: ItemWhiteList): number {
    let sum: number = 0;
    for (let i = 0; i < cartItems.length; i++) {
      const item = cartItems[i];
      const matches: boolean = group.whiteList.some((p: Product) => productMatches(p, item.product));
      if (matches) {
        sum += item.quantity;
      }
    }
    return sum;
  }

  /**
   * Sum of per-group cart quantities across all non-canceled execution
   * records: a group's quantity budget is consumed by every non-canceled
   * order containing a matching product.
   * @param executions - The execution records ledger
   * @param group - The whitelist group to match products against
   * @returns The accumulated quantity consuming the group's budget
   */
  function computeAccumulatedQuantityInGroup(executions: ExecutionRecord[], group: ItemWhiteList): number {
    let sum: number = 0;
    for (let i = 0; i < executions.length; i++) {
      if (executions[i].status !== ExecutionStatus.CANCELED) {
        sum += cartQuantityInGroup(executions[i].execution.executionContent.cartItems, group);
      }
    }
    return sum;
  }

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
                                   now?: number): IntentExecutionContext {
    raise(validateExecution(execution));
    if (!execution.id) {
      execution.id = util.generateRandomUUID();
    }
    const duplicateExists: boolean = context.executions.some(
      (r: ExecutionRecord) => r.executionId === execution.id);
    if (duplicateExists) {
      a4pThrow(A4pErrorCode.A4P_DUPLICATE_ID, 'Execution ID already exists: ' + execution.id);
    }
    if (context.status !== IntentExecutionStatus.ACTIVE) {
      a4pThrow(A4pErrorCode.A4P_INVALID_STATUS,
        'createExecution requires an active intent, current status: ' + context.status);
    }

    const l1: IntentL1 = context.authorizedIntent.intentL2.intentL1;
    const constraints: PaymentConstraints = l1.structuredIntent.constraints;
    const nowMs: number = now !== undefined ? now : Date.now();

    if (execution.version !== l1.version) {
      a4pThrow(A4pErrorCode.A4P_UNSUPPORTED_VERSION,
        'Execution version ' + execution.version + ' does not match intent version ' + l1.version);
    }
    if (execution.action !== l1.structuredIntent.action) {
      a4pThrow(A4pErrorCode.A4P_INVALID_PARAMETER,
        'Execution action ' + execution.action + ' does not match intent action ' + l1.structuredIntent.action);
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
    if (execution.executionContent.currency !== constraints.currency) {
      a4pThrow(A4pErrorCode.A4P_INVALID_PARAMETER,
        'Currency mismatch: order is ' + execution.executionContent.currency +
        ', intent constraint is ' + constraints.currency);
    }

    if (l1.structuredIntent.action === IntentActionType.AI_PAY) {
      raise(enforceWhitelistScope(context, execution.executionContent.cartItems));
      const resource: ShoppingItems = l1.structuredIntent.resource;
      for (let g = 0; g < resource.itemsWhiteList.length; g++) {
        const group: ItemWhiteList = resource.itemsWhiteList[g];
        const accumulatedQuantity: number = computeAccumulatedQuantityInGroup(context.executions, group);
        const newQuantity: number = cartQuantityInGroup(execution.executionContent.cartItems, group);
        if (accumulatedQuantity + newQuantity > group.quantity) {
          a4pThrow(A4pErrorCode.A4P_PRODUCT_QUANTITY_EXCEEDED,
            'Total quantity exceeds authorized for whitelist group ' + g + ': ' +
            (accumulatedQuantity + newQuantity) + ' > ' + group.quantity);
        }
      }
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

    const record: ExecutionRecord = {
      executionId: execution.id,
      status: ExecutionStatus.PENDING,
      reservedAt: nowMs,
      execution: deepClone(execution),
    };
    if (currentPeriodIndex !== undefined) {
      record.periodIndex = currentPeriodIndex;
    }

    context.executions.push(record);
    recomputeAccounting(context, nowMs);

    return context;
  }

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
                                    now?: number): IntentExecutionContext {
    if (context.status !== IntentExecutionStatus.ACTIVE && context.status !== IntentExecutionStatus.REVOKED) {
      a4pThrow(A4pErrorCode.A4P_INVALID_STATUS,
        'completeExecution requires an active or revoked intent, current status: ' + context.status);
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
    // Auto-complete requires a count cap: without maxExecuteCount the
    // intent stays open for further executions even when every record so
    // far is finalized.
    const countCap: number | undefined =
      context.authorizedIntent.intentL2.intentL1.structuredIntent.constraints.maxExecuteCount;
    if (allDone && countCap !== undefined && computeExecutionCount(context.executions) >= countCap) {
      context.status = IntentExecutionStatus.COMPLETED;
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
   * Allowed when the context is ACTIVE or REVOKED (so pending executions
   * reserved before revocation can still be finalized).
   * @param context - The execution context containing the pending execution
   * @param executionId - The ID of the execution to cancel
   * @param now - Optional cancellation timestamp in epoch milliseconds; defaults to Date.now()
   * @returns The updated execution context with quota released
   * @throws A4pError on state failure
   */
  export function cancelExecution(context: IntentExecutionContext, executionId: string,
                                  now?: number): IntentExecutionContext {
    if (context.status !== IntentExecutionStatus.ACTIVE && context.status !== IntentExecutionStatus.REVOKED) {
      a4pThrow(A4pErrorCode.A4P_INVALID_STATUS,
        'cancelExecution requires an active or revoked intent, current status: ' + context.status);
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
