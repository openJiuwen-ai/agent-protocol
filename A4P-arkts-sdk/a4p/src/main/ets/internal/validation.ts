/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

import { CreateIntentRequest, IntentIssueContext, A4pErrorCode } from '../models/common';
import { PaymentConstraints } from '../models/constraint';
import { StructuredIntent, IntentActionType, AlgorithmType, IntentL1, IntentL2, IntentL3, SignatureInfo } from '../models/intent';
import { UserIdentity, AgentIdentity, ServiceProviderIdentity, SigningEnv } from '../models/identity';
import {
  Execution, Order, IntentExecutionContext, ExecutionRecord, PaymentExecutionInfo,
  IntentExecutionStatus, ExecutionStatus
} from '../models/execution';
import { Product, ItemWhiteList, ShoppingItems, CartItem } from '../models/product';

// All protocol numbers must be safe integers: beyond 2^53 - 1 doubles lose
// precision and canonical serialization across implementations would break.
const MAX_SAFE: number = Number.MAX_SAFE_INTEGER;

// Period durations are converted to milliseconds (× 1000) when computing
// period indexes; beyond this bound the product would leave the
// safe-integer range.
const MAX_PERIOD_SECONDS: number = Math.floor(MAX_SAFE / 1000);

/**
 * Failure shape returned by validators: a code plus the specific cause of
 * the failure. When raised, the cause is recorded via hilog and the thrown
 * A4pError carries only the fixed message for the code.
 */
export class A4pValidationFailure {
  /** A4P error code identifying the failure category */
  readonly code: A4pErrorCode;
  /** Specific cause of the failure (logged via hilog, not thrown) */
  readonly detail: string;

  /**
   * @param code - The A4P error code for this failure
   * @param detail - The specific cause distinguishing this failure
   */
  constructor(code: A4pErrorCode, detail: string) {
    this.code = code;
    this.detail = detail;
  }
}

/** Validation outcome: a failure describing the first problem found, or null when the input is valid. */
type Validation = A4pValidationFailure | null;

/** Whether v is a non-negative safe integer (undefined is not). */
function isNonNegativeInteger(v: number | undefined): boolean {
  return typeof v === 'number' && v === Math.floor(v) && v >= 0 && v <= MAX_SAFE;
}

/** Whether v is a positive safe integer (undefined is not). */
function isPositiveInteger(v: number | undefined): boolean {
  return typeof v === 'number' && v === Math.floor(v) && v > 0 && v <= MAX_SAFE;
}

/** Whether v is a safe integer of either sign (undefined is not). */
function isSafeInteger(v: number | undefined): boolean {
  return typeof v === 'number' && v === Math.floor(v) && v >= -MAX_SAFE && v <= MAX_SAFE;
}

/** Whether v is a defined, non-blank string (whitespace-only fails). */
function isNonBlankString(v: string | undefined): boolean {
  return typeof v === 'string' && v.trim().length > 0;
}

/** Builds a validation failure with the given code and specific cause. */
function fail(code: A4pErrorCode, detail: string): Validation {
  return new A4pValidationFailure(code, detail);
}

/**
 * Whether the value is a defined IntentActionType member. A foreign value
 * (e.g. a tampered or out-of-enum number) is not a valid action.
 */
function isValidIntentActionType(action: IntentActionType): boolean {
  return action === IntentActionType.AI_PAY;
}

/**
 * Whether the value is a defined AlgorithmType member. A foreign value
 * (e.g. a tampered algorithm string) is not a valid alg: alg is detached
 * from the signed bytes, so admitting unknown values structurally would
 * defer the alg/key-confusion defense entirely to the verifier.
 */
function isValidAlgorithmType(alg: AlgorithmType): boolean {
  return alg === AlgorithmType.RSA3072;
}

/**
 * Validates a PaymentConstraints object: amounts, validity window, and the
 * period rule. Optional caps (`singleOrderMaxAmount`, `maxExecuteCount`)
 * are validated only when present; the period rule is all-or-nothing —
 * all three fields provided (periodic) or all three omitted
 * (non-periodic), never partial.
 */
function validatePaymentConstraints(pc: PaymentConstraints): Validation {
  if (pc.singleOrderMaxAmount !== undefined && !isPositiveInteger(pc.singleOrderMaxAmount)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'singleOrderMaxAmount must be a positive integer');
  }
  if (!isPositiveInteger(pc.totalMaxAmount)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'totalMaxAmount must be a positive integer');
  }
  if (!isNonBlankString(pc.currency)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'currency must not be blank');
  }
  if (!isNonNegativeInteger(pc.validFrom)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'validFrom must be a non-negative integer');
  }
  if (!isNonNegativeInteger(pc.validUntil)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'validUntil must be a non-negative integer');
  }
  if (pc.validFrom > pc.validUntil) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'validFrom must not be later than validUntil');
  }
  if (pc.maxExecuteCount !== undefined && !isPositiveInteger(pc.maxExecuteCount)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'maxExecuteCount must be a positive integer');
  }
  const hasAnchorTime: boolean = pc.anchorTime !== undefined;
  const hasPeriodSeconds: boolean = pc.periodSeconds !== undefined;
  const hasPerPeriodCount: boolean = pc.maxExecutionCountPerPeriod !== undefined;
  if (hasAnchorTime || hasPeriodSeconds || hasPerPeriodCount) {
    if (!hasAnchorTime || !hasPeriodSeconds || !hasPerPeriodCount) {
      return fail(A4pErrorCode.A4P_INVALID_PARAMETER,
        'anchorTime, periodSeconds and maxExecutionCountPerPeriod must be provided together');
    }
    if (!isNonNegativeInteger(pc.anchorTime)) {
      return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'anchorTime must be a non-negative integer');
    }
    // Reservations happen at now >= validFrom: an anchor later than
    // validFrom would give pre-anchor reservations negative period
    // indexes, each with a fresh per-period budget, hollowing out the
    // per-period cap.
    if (pc.anchorTime > pc.validFrom) {
      return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'anchorTime must not be later than validFrom');
    }
    if (!isPositiveInteger(pc.periodSeconds)) {
      return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'periodSeconds must be a positive integer');
    }
    if (pc.periodSeconds > MAX_PERIOD_SECONDS) {
      return fail(A4pErrorCode.A4P_INVALID_PARAMETER,
        'periodSeconds must not exceed ' + MAX_PERIOD_SECONDS + ' (MAX_SAFE_INTEGER / 1000)');
    }
    if (!isPositiveInteger(pc.maxExecutionCountPerPeriod)) {
      return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'maxExecutionCountPerPeriod must be a positive integer');
    }
  }
  return null;
}

/**
 * Whether two products refer to the same item: seller id, spuId, and
 * skuId must all match (the whitelist matching key). Shared by the
 * structural duplicate checks here and the runtime scope/template
 * matching in api/a4p.ts, so both always agree on the key.
 */
export function productMatches(a: Product, b: Product): boolean {
  return a.seller.id === b.seller.id && a.spusku.spuId === b.spusku.spuId && a.spusku.skuId === b.spusku.skuId;
}

/**
 * Validates a product's identity: a non-null seller with a non-blank id,
 * and a non-null spusku carrying at least one non-blank id (a provided id
 * must not be blank). seller id + spuId + skuId is the whitelist matching
 * key (see productMatches), so blank or absent ids would let products
 * match each other vacuously and hollow out the authorized scope.
 */
function validateProduct(product: Product, field: string): Validation {
  if (!product.seller) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, field + '.seller must not be null');
  }
  if (!isNonBlankString(product.seller.id)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, field + '.seller.id must not be blank');
  }
  if (!product.spusku) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, field + '.spusku must not be null');
  }
  if (product.spusku.spuId !== undefined && !isNonBlankString(product.spusku.spuId)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, field + '.spusku.spuId must not be blank when provided');
  }
  if (product.spusku.skuId !== undefined && !isNonBlankString(product.spusku.skuId)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, field + '.spusku.skuId must not be blank when provided');
  }
  if (product.spusku.spuId === undefined && product.spusku.skuId === undefined) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER,
      field + '.spusku must carry at least one of spuId or skuId');
  }
  return null;
}

/**
 * Validates a whitelist group: non-empty product list, no duplicate
 * products, and positive quantity. A product may appear at most once:
 * duplicates would make the OR semantics of the group ambiguous.
 */
function validateItemWhiteList(group: ItemWhiteList, field: string): Validation {
  if (!group) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, field + ' must not be null');
  }
  if (!group.whiteList || group.whiteList.length === 0) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, field + '.whiteList must not be empty');
  }
  for (let i = 0; i < group.whiteList.length; i++) {
    const product = group.whiteList[i];
    if (!product) {
      return fail(A4pErrorCode.A4P_INVALID_PARAMETER, field + '.whiteList[' + i + '] must not be null');
    }
    const productErr = validateProduct(product, field + '.whiteList[' + i + ']');
    if (productErr) {
      return productErr;
    }
    for (let j = 0; j < i; j++) {
      if (productMatches(product, group.whiteList[j])) {
        return fail(A4pErrorCode.A4P_INVALID_PARAMETER,
          field + '.whiteList[' + i + '] duplicates whiteList[' + j + ']: a product may appear at most once per group');
      }
    }
  }
  if (!isPositiveInteger(group.quantity)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, field + '.quantity must be a positive integer');
  }
  return null;
}

/**
 * Validates the resource: a non-empty list of valid whitelist groups with
 * no product shared by two groups. The resource is an order template and
 * a cart item finds its group by product alone, so a product must
 * uniquely determine its group.
 */
function validateShoppingItems(resource: ShoppingItems, field: string): Validation {
  if (!resource.itemsWhiteList || resource.itemsWhiteList.length === 0) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, field + '.itemsWhiteList must not be empty');
  }
  for (let i = 0; i < resource.itemsWhiteList.length; i++) {
    const groupErr = validateItemWhiteList(resource.itemsWhiteList[i], field + '.itemsWhiteList[' + i + ']');
    if (groupErr) {
      return groupErr;
    }
    for (let j = 0; j < i; j++) {
      const earlier: ItemWhiteList = resource.itemsWhiteList[j];
      const current: ItemWhiteList = resource.itemsWhiteList[i];
      for (let a = 0; a < current.whiteList.length; a++) {
        for (let b = 0; b < earlier.whiteList.length; b++) {
          if (productMatches(current.whiteList[a], earlier.whiteList[b])) {
            return fail(A4pErrorCode.A4P_INVALID_PARAMETER,
              field + '.itemsWhiteList[' + i + '].whiteList[' + a + '] duplicates itemsWhiteList[' + j +
              '].whiteList[' + b + ']: a product may appear in at most one group');
          }
        }
      }
    }
  }
  return null;
}

/** Validates a cart item list: non-empty, valid products, positive quantities. */
function validateCartItems(cartItems: CartItem[], field: string): Validation {
  if (!cartItems || cartItems.length === 0) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, field + ' must not be empty');
  }
  for (let i = 0; i < cartItems.length; i++) {
    const item = cartItems[i];
    if (!item) {
      return fail(A4pErrorCode.A4P_INVALID_PARAMETER, field + '[' + i + '] must not be null');
    }
    if (!item.product) {
      return fail(A4pErrorCode.A4P_INVALID_PARAMETER, field + '[' + i + '].product must not be null');
    }
    const productErr = validateProduct(item.product, field + '[' + i + '].product');
    if (productErr) {
      return productErr;
    }
    if (!isPositiveInteger(item.quantity)) {
      return fail(A4pErrorCode.A4P_INVALID_PARAMETER, field + '[' + i + '].quantity must be a positive integer');
    }
  }
  return null;
}

/** Validates a StructuredIntent: action, resource, and constraints. */
function validateStructuredIntent(si: StructuredIntent): Validation {
  if (!isValidIntentActionType(si.action)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'structuredIntent.action must be a valid IntentActionType');
  }
  if (!si.resource) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'structuredIntent.resource must not be null');
  }
  const resourceErr = validateShoppingItems(si.resource, 'structuredIntent.resource');
  if (resourceErr) {
    return resourceErr;
  }
  if (!si.constraints) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'structuredIntent.constraints must not be null');
  }
  return validatePaymentConstraints(si.constraints);
}

/** Validates a UserIdentity: non-empty provider and uid. */
function validateUserIdentity(ui: UserIdentity): Validation {
  if (!isNonBlankString(ui.identityProvider)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'userIdentity.identityProvider must not be blank');
  }
  if (!isNonBlankString(ui.id)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'userIdentity.uid must not be blank');
  }
  return null;
}

/** Validates an AgentIdentity: non-empty provider and id. */
function validateAgentIdentity(ai: AgentIdentity): Validation {
  if (!isNonBlankString(ai.identityProvider)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'agentIdentity.identityProvider must not be blank');
  }
  if (!isNonBlankString(ai.id)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'agentIdentity.id must not be blank');
  }
  return null;
}

/** Validates a ServiceProviderIdentity: non-empty id. */
function validateServiceProviderIdentity(spi: ServiceProviderIdentity): Validation {
  if (!isNonBlankString(spi.id)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'serviceProviderIdentity.id must not be blank');
  }
  return null;
}

/**
 * Validates a SigningEnv object.
 * @param env - The signing environment to validate
 * @returns A validation failure if invalid, or null if valid
 */
export function validateSigningEnv(env: SigningEnv): Validation {
  if (!env) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'signingEnv must not be null');
  }
  if (!env.device) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'signingEnv.device must not be null');
  }
  if (!env.app) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'signingEnv.app must not be null');
  }
  return null;
}

/** Validates an Order: non-empty id and currency, positive amount, valid cart, timestamp. */
function validateOrder(order: Order): Validation {
  if (!isNonBlankString(order.orderId)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'order.orderId must not be blank');
  }
  if (!isPositiveInteger(order.amount)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'order.amount must be a positive integer');
  }
  if (!isNonBlankString(order.currency)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'order.currency must not be blank');
  }
  if (!order.cartItems) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'order.cartItems must not be null');
  }
  const cartErr = validateCartItems(order.cartItems, 'order.cartItems');
  if (cartErr) {
    return cartErr;
  }
  if (!isNonNegativeInteger(order.timestamp)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'order.timestamp must be a non-negative integer');
  }
  return null;
}

/**
 * Validates an injected clock reading: an explicitly provided `now` must be
 * a non-negative safe integer (epoch milliseconds), so every stamped
 * timestamp and derived period figure stays in the canonical numeric range.
 * @param now - The evaluation instant to validate
 * @returns A validation failure when now is out of range, or null when valid
 */
export function validateNow(now: number): Validation {
  if (!isNonNegativeInteger(now)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER,
      'now must be a non-negative safe integer (epoch milliseconds)');
  }
  return null;
}

/**
 * Validates an Execution object.
 * @param exec - The execution to validate
 * @returns A validation failure if invalid, or null if valid
 */
export function validateExecution(exec: Execution): Validation {
  if (!exec) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'execution must not be null');
  }
  if (!isValidIntentActionType(exec.action)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'execution.action must be a valid IntentActionType');
  }
  if (exec.id !== undefined && !isNonBlankString(exec.id)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER,
      'execution.id must be a non-blank string when provided (an absent id is generated)');
  }
  if (!exec.executionContent) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'execution.executionContent must not be null');
  }
  return validateOrder(exec.executionContent);
}

/**
 * Validates that an execution context object is present: the execution APIs
 * dereference the caller's context (status, snapshot, ledger), so a null
 * context must surface as a validation failure — and then an A4pError —
 * instead of a raw TypeError.
 * @param context - The execution context to validate
 * @returns A validation failure when the context is null, or null when present
 */
export function validateExecutionContext(context: IntentExecutionContext): Validation {
  if (!context) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'context must not be null');
  }
  return null;
}

/**
 * Validates a CreateIntentRequest object.
 * @param request - The request to validate
 * @returns A validation failure if invalid, or null if valid
 */
export function validateCreateIntentRequest(request: CreateIntentRequest): Validation {
  if (!request) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'request must not be null');
  }
  if (!isNonBlankString(request.intentConversation)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'intentConversation must not be blank');
  }
  if (!isNonBlankString(request.intentSummary)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'intentSummary must not be blank');
  }
  if (request.intentId !== undefined && !isNonBlankString(request.intentId)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'intentId must not be blank when provided');
  }
  if (!request.structuredIntent) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'structuredIntent must not be null');
  }
  return validateStructuredIntent(request.structuredIntent);
}

/**
 * Validates an IntentIssueContext object.
 * @param ctx - The issue context to validate
 * @returns A validation failure if invalid, or null if valid
 */
export function validateIssueContext(ctx: IntentIssueContext): Validation {
  if (!ctx) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'issueContext must not be null');
  }
  if (!ctx.userIdentity) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'issueContext.userIdentity must not be null');
  }
  const uiErr = validateUserIdentity(ctx.userIdentity);
  if (uiErr) {
    return uiErr;
  }
  if (!ctx.agentIdentity) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'issueContext.agentIdentity must not be null');
  }
  const aiErr = validateAgentIdentity(ctx.agentIdentity);
  if (aiErr) {
    return aiErr;
  }
  if (!ctx.serviceProviderIdentity) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'issueContext.serviceProviderIdentity must not be null');
  }
  const spiErr = validateServiceProviderIdentity(ctx.serviceProviderIdentity);
  if (spiErr) {
    return spiErr;
  }
  return null;
}

/**
 * Validates the signature information of an intent level: `alg` must be a
 * defined AlgorithmType member, and `keyIdentifier` and the signature
 * must be present and non-blank. The field names use the L1 numbering
 * (`intentL1`), the L2/L3 validators pass their own prefixes. Also used
 * to check a Signer's return value before it is attached to an intent
 * (see runSigner in api/a4p.ts).
 */
export function validateSignatureInfo(info: SignatureInfo, field: string): Validation {
  if (!info) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, field + '.sign must not be null');
  }
  if (!isValidAlgorithmType(info.alg)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, field + '.sign.alg must be a valid AlgorithmType');
  }
  if (!isNonBlankString(info.keyIdentifier)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, field + '.sign.keyIdentifier must not be blank');
  }
  if (!isNonBlankString(info.sign)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, field + '.sign.sign must not be blank');
  }
  return null;
}

/**
 * Validates an IntentL1 object structurally.
 * @param intent - The Level 1 intent to validate
 * @returns A validation failure if invalid, or null if valid
 */
export function validateIntentL1(intent: IntentL1): Validation {
  if (!intent) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'intentL1 must not be null');
  }
  if (!isNonBlankString(intent.version)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'intentL1.version must not be blank');
  }
  if (!isNonBlankString(intent.intentId)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'intentL1.intentId must not be blank');
  }
  if (!isNonBlankString(intent.intentConversation)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'intentL1.intentConversation must not be blank');
  }
  if (!isNonBlankString(intent.intentSummary)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'intentL1.intentSummary must not be blank');
  }
  if (!intent.structuredIntent) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'intentL1.structuredIntent must not be null');
  }
  const siErr = validateStructuredIntent(intent.structuredIntent);
  if (siErr) {
    return siErr;
  }
  if (!isNonNegativeInteger(intent.timestamp)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'intentL1.timestamp must be a non-negative integer');
  }
  const signErr = validateSignatureInfo(intent.sign, 'intentL1');
  if (signErr) {
    return signErr;
  }
  return null;
}

/**
 * Validates an IntentL2 object structurally, including its embedded L1.
 * @param intent - The Level 2 intent to validate
 * @returns A validation failure if invalid, or null if valid
 */
export function validateIntentL2(intent: IntentL2): Validation {
  if (!intent) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'intentL2 must not be null');
  }
  if (!isNonBlankString(intent.version)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'intentL2.version must not be blank');
  }
  if (!intent.intentL1) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'intentL2.intentL1 must not be null');
  }
  const l1Err = validateIntentL1(intent.intentL1);
  if (l1Err) {
    return l1Err;
  }
  if (!intent.userIdentity) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'intentL2.userIdentity must not be null');
  }
  const uiErr = validateUserIdentity(intent.userIdentity);
  if (uiErr) {
    return uiErr;
  }
  if (!intent.agentIdentity) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'intentL2.agentIdentity must not be null');
  }
  const aiErr = validateAgentIdentity(intent.agentIdentity);
  if (aiErr) {
    return aiErr;
  }
  if (!intent.serviceProviderIdentity) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'intentL2.serviceProviderIdentity must not be null');
  }
  const spiErr = validateServiceProviderIdentity(intent.serviceProviderIdentity);
  if (spiErr) {
    return spiErr;
  }
  if (!isNonNegativeInteger(intent.timestamp)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'intentL2.timestamp must be a non-negative integer');
  }
  // Chain timestamps are monotonic: issuance cannot predate the intent
  // it endorses.
  if (intent.timestamp < intent.intentL1.timestamp) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER,
      'intentL2.timestamp must not be earlier than intentL1.timestamp');
  }
  const signErr = validateSignatureInfo(intent.sign, 'intentL2');
  if (signErr) {
    return signErr;
  }
  return null;
}

/**
 * Validates an IntentL3 object structurally, including its embedded L2 and L1.
 * @param intent - The Level 3 intent to validate
 * @returns A validation failure if invalid, or null if valid
 */
export function validateIntentL3(intent: IntentL3): Validation {
  if (!intent) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'intentL3 must not be null');
  }
  if (!isNonBlankString(intent.version)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'intentL3.version must not be blank');
  }
  if (!intent.intentL2) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'intentL3.intentL2 must not be null');
  }
  const l2Err = validateIntentL2(intent.intentL2);
  if (l2Err) {
    return l2Err;
  }
  if (!intent.signingEnv) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'intentL3.signingEnv must not be null');
  }
  const envErr = validateSigningEnv(intent.signingEnv);
  if (envErr) {
    return envErr;
  }
  if (!isNonNegativeInteger(intent.timestamp)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'intentL3.timestamp must be a non-negative integer');
  }
  // Chain timestamps are monotonic: user authorization cannot predate the
  // issuance it authorizes.
  if (intent.timestamp < intent.intentL2.timestamp) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER,
      'intentL3.timestamp must not be earlier than intentL2.timestamp');
  }
  const signErr = validateSignatureInfo(intent.sign, 'intentL3');
  if (signErr) {
    return signErr;
  }
  return null;
}

/**
 * Whether the value is a defined ExecutionStatus member (a foreign value
 * is not a valid record status).
 */
function isValidExecutionStatus(status: ExecutionStatus): boolean {
  return status === ExecutionStatus.PENDING || status === ExecutionStatus.COMPLETED ||
    status === ExecutionStatus.CANCELED;
}

/**
 * Whether the value is a defined IntentExecutionStatus member (a foreign
 * value is not a valid lifecycle status).
 */
function isValidIntentExecutionStatus(status: IntentExecutionStatus): boolean {
  return status === IntentExecutionStatus.ACTIVE || status === IntentExecutionStatus.CONSUMED ||
    status === IntentExecutionStatus.REVOKED || status === IntentExecutionStatus.EXPIRED;
}

/**
 * Validates an execution record: a non-blank executionId, a defined
 * status, a non-negative reservedAt, the status-bound finalization
 * timestamps (completedAt on a COMPLETED record, canceledAt on a CANCELED
 * one — and only there), and the embedded execution itself.
 * @param record - The execution record to validate
 * @param field - The field path prefix for error messages
 * @returns A validation failure if invalid, or null if valid
 */
function validateExecutionRecord(record: ExecutionRecord, field: string): Validation {
  if (!record) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, field + ' must not be null');
  }
  if (!isNonBlankString(record.executionId)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, field + '.executionId must not be blank');
  }
  if (!isValidExecutionStatus(record.status)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, field + '.status must be a valid ExecutionStatus');
  }
  // Reservations happen at now >= validFrom and the anchor is never later
  // than validFrom, so a record's period index is never negative. Whether
  // the index must be present at all is a periodicity matter, checked by
  // the context validator, which knows the intent's period rule.
  if (record.periodIndex !== undefined && !isNonNegativeInteger(record.periodIndex)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER,
      field + '.periodIndex must be a non-negative integer when present');
  }
  if (!isNonNegativeInteger(record.reservedAt)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, field + '.reservedAt must be a non-negative integer');
  }
  if (record.completedAt !== undefined && !isNonNegativeInteger(record.completedAt)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER,
      field + '.completedAt must be a non-negative integer when present');
  }
  if (record.canceledAt !== undefined && !isNonNegativeInteger(record.canceledAt)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER,
      field + '.canceledAt must be a non-negative integer when present');
  }
  // Finalization timestamps are status-bound: completeExecution stamps
  // completedAt, cancelExecution stamps canceledAt, and no other
  // transition exists, so any other pairing is corrupted data.
  if (record.status === ExecutionStatus.COMPLETED) {
    if (record.completedAt === undefined) {
      return fail(A4pErrorCode.A4P_INVALID_PARAMETER,
        field + '.completedAt must be present on a COMPLETED record');
    }
  } else if (record.completedAt !== undefined) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER,
      field + '.completedAt must be present only on a COMPLETED record');
  }
  if (record.status === ExecutionStatus.CANCELED) {
    if (record.canceledAt === undefined) {
      return fail(A4pErrorCode.A4P_INVALID_PARAMETER,
        field + '.canceledAt must be present on a CANCELED record');
    }
  } else if (record.canceledAt !== undefined) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER,
      field + '.canceledAt must be present only on a CANCELED record');
  }
  if (!record.execution) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, field + '.execution must not be null');
  }
  const execErr = validateExecution(record.execution);
  if (execErr) {
    return execErr;
  }
  // The record's key and the stored execution's id are the same id by
  // construction (createExecution resolves it once); a mismatch is
  // corrupted data.
  if (record.execution.id !== record.executionId) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER,
      field + '.execution.id must equal ' + field + '.executionId');
  }
  return null;
}

/**
 * Validates a payment execution info snapshot: leftAmount is required,
 * the optional figures are checked when present. The presence rules tied
 * to the intent's caps (period figures for periodic intents,
 * leftExecutionCount with a count cap) are checked by the context
 * validator, which knows the intent's constraints.
 * @param info - The execution info snapshot to validate
 * @param field - The field path prefix for error messages
 * @returns A validation failure if invalid, or null if valid
 */
function validatePaymentExecutionInfo(info: PaymentExecutionInfo, field: string): Validation {
  if (!info) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, field + ' must not be null');
  }
  // The snapshot period index may be negative: activateIntent applies no
  // validity-window check, so an activation before the anchor snapshots
  // the pre-anchor period. Record indexes, in contrast, are reserved at
  // now >= validFrom >= anchorTime and are never negative.
  if (info.periodIndex !== undefined && !isSafeInteger(info.periodIndex)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER,
      field + '.periodIndex must be a safe integer when present');
  }
  if (info.leftPeriodExecutionCount !== undefined && !isNonNegativeInteger(info.leftPeriodExecutionCount)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER,
      field + '.leftPeriodExecutionCount must be a non-negative integer when present');
  }
  if (info.leftExecutionCount !== undefined && !isNonNegativeInteger(info.leftExecutionCount)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER,
      field + '.leftExecutionCount must be a non-negative integer when present');
  }
  if (!isNonNegativeInteger(info.leftAmount)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, field + '.leftAmount must be a non-negative integer');
  }
  return null;
}

/**
 * Validates an IntentExecutionContext object structurally: the full
 * authorized intent chain (validateIntentL3), the intentId derivation,
 * a defined lifecycle status, every ledger record, and the accounting
 * snapshot — including the cap-tied presence rules: records carry a
 * periodIndex exactly when the intent is periodic, and the snapshot
 * carries its period figures exactly when periodic and
 * leftExecutionCount exactly when the intent has a maxExecuteCount cap.
 * Used by the codec when deserializing a context (see
 * A4pCodec.deserializeIntentExecutionContext); the execution APIs keep
 * the lighter presence-only validateExecutionContext.
 * @param context - The execution context to validate
 * @returns A validation failure if invalid, or null if valid
 */
export function validateIntentExecutionContext(context: IntentExecutionContext): Validation {
  if (!context) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'context must not be null');
  }
  if (!isNonBlankString(context.version)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'context.version must not be blank');
  }
  if (!isNonBlankString(context.intentId)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'context.intentId must not be blank');
  }
  if (!context.authorizedIntent) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'context.authorizedIntent must not be null');
  }
  const intentErr = validateIntentL3(context.authorizedIntent);
  if (intentErr) {
    return intentErr;
  }
  const l1: IntentL1 = context.authorizedIntent.intentL2.intentL1;
  // intentId is derived from the authorized chain at activation; a
  // mismatched id is corrupted data, not an alternative spelling.
  if (context.intentId !== l1.intentId) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER,
      'context.intentId must equal intentL1.intentId of the authorized chain');
  }
  if (!isValidIntentExecutionStatus(context.status)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'context.status must be a valid IntentExecutionStatus');
  }
  // The ledger may be empty (nothing reserved yet) but must be an array.
  if (!Array.isArray(context.executions)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'context.executions must be an array');
  }
  // The period rule is all-or-nothing (validated with the intent chain),
  // so the anchor alone decides periodicity.
  const periodic: boolean = l1.structuredIntent.constraints.anchorTime !== undefined;
  for (let i = 0; i < context.executions.length; i++) {
    const recordErr = validateExecutionRecord(context.executions[i], 'context.executions[' + i + ']');
    if (recordErr) {
      return recordErr;
    }
    if (periodic && context.executions[i].periodIndex === undefined) {
      return fail(A4pErrorCode.A4P_INVALID_PARAMETER,
        'context.executions[' + i + '].periodIndex must be present on a periodic intent');
    }
    if (!periodic && context.executions[i].periodIndex !== undefined) {
      return fail(A4pErrorCode.A4P_INVALID_PARAMETER,
        'context.executions[' + i + '].periodIndex must be absent on a non-periodic intent');
    }
  }
  if (!context.executionInfo) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'context.executionInfo must not be null');
  }
  const infoErr = validatePaymentExecutionInfo(context.executionInfo, 'context.executionInfo');
  if (infoErr) {
    return infoErr;
  }
  if (periodic && (context.executionInfo.periodIndex === undefined ||
    context.executionInfo.leftPeriodExecutionCount === undefined)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER,
      'context.executionInfo.periodIndex and leftPeriodExecutionCount must be present on a periodic intent');
  }
  if (!periodic && (context.executionInfo.periodIndex !== undefined ||
    context.executionInfo.leftPeriodExecutionCount !== undefined)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER,
      'context.executionInfo.periodIndex and leftPeriodExecutionCount must be absent on a non-periodic intent');
  }
  const countCap: number | undefined = l1.structuredIntent.constraints.maxExecuteCount;
  if (countCap !== undefined && context.executionInfo.leftExecutionCount === undefined) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER,
      'context.executionInfo.leftExecutionCount must be present when the intent carries maxExecuteCount');
  }
  if (countCap === undefined && context.executionInfo.leftExecutionCount !== undefined) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER,
      'context.executionInfo.leftExecutionCount must be absent when the intent carries no maxExecuteCount');
  }
  return null;
}
