/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

import { CreateIntentRequest, IntentIssueContext, A4pErrorCode } from '../models/common';
import { PaymentConstraints } from '../models/constraint';
import { StructuredIntent, IntentActionType, IntentL1, IntentL2, IntentL3, SignatureInfo } from '../models/intent';
import { UserIdentity, AgentIdentity, ServiceProviderIdentity, SigningEnv } from '../models/identity';
import { Execution, Order } from '../models/execution';
import { Product, ItemWhiteList, ShoppingItems, CartItem } from '../models/product';

// All protocol numbers must be safe integers: beyond 2^53 - 1 doubles lose
// precision and canonical serialization across implementations would break.
const MAX_SAFE: number = Number.MAX_SAFE_INTEGER;

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
  if (!pc.currency) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'currency must not be empty');
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
    if (!isPositiveInteger(pc.periodSeconds)) {
      return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'periodSeconds must be a positive integer');
    }
    if (!isPositiveInteger(pc.maxExecutionCountPerPeriod)) {
      return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'maxExecutionCountPerPeriod must be a positive integer');
    }
  }
  return null;
}

/** Validates that a product carries non-null seller and spusku. */
function validateProduct(product: Product, field: string): Validation {
  if (!product.seller) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, field + '.seller must not be null');
  }
  if (!product.spusku) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, field + '.spusku must not be null');
  }
  return null;
}

/** Validates a whitelist group: non-empty product list and positive quantity. */
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
  }
  if (!isPositiveInteger(group.quantity)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, field + '.quantity must be a positive integer');
  }
  return null;
}

/** Validates the resource: a non-empty list of valid whitelist groups. */
function validateShoppingItems(resource: ShoppingItems, field: string): Validation {
  if (!resource.itemsWhiteList || resource.itemsWhiteList.length === 0) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, field + '.itemsWhiteList must not be empty');
  }
  for (let i = 0; i < resource.itemsWhiteList.length; i++) {
    const groupErr = validateItemWhiteList(resource.itemsWhiteList[i], field + '.itemsWhiteList[' + i + ']');
    if (groupErr) {
      return groupErr;
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
  if (!ui.identityProvider) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'userIdentity.identityProvider must not be empty');
  }
  if (!ui.uid) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'userIdentity.uid must not be empty');
  }
  return null;
}

/** Validates an AgentIdentity: non-empty provider and id. */
function validateAgentIdentity(ai: AgentIdentity): Validation {
  if (!ai.identityProvider) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'agentIdentity.identityProvider must not be empty');
  }
  if (!ai.id) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'agentIdentity.id must not be empty');
  }
  return null;
}

/** Validates a ServiceProviderIdentity: non-empty id. */
function validateServiceProviderIdentity(spi: ServiceProviderIdentity): Validation {
  if (!spi.id) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'serviceProviderIdentity.id must not be empty');
  }
  return null;
}

/**
 * Validates a SigningEnv object.
 * @param env - The signing environment to validate
 * @returns A validation failure if invalid, or null if valid
 */
export function validateSigningEnv(env: SigningEnv): Validation {
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
  if (!order.orderId) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'order.orderId must not be empty');
  }
  if (!isPositiveInteger(order.amount)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'order.amount must be a positive integer');
  }
  if (!order.currency) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'order.currency must not be empty');
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
 * Validates an Execution object.
 * @param exec - The execution to validate
 * @returns A validation failure if invalid, or null if valid
 */
export function validateExecution(exec: Execution): Validation {
  if (!isValidIntentActionType(exec.action)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'execution.action must be a valid IntentActionType');
  }
  if (!exec.version) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'execution.version must not be empty');
  }
  if (!exec.executionContent) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'execution.executionContent must not be null');
  }
  return validateOrder(exec.executionContent);
}

/**
 * Validates a CreateIntentRequest object.
 * @param request - The request to validate
 * @returns A validation failure if invalid, or null if valid
 */
export function validateCreateIntentRequest(request: CreateIntentRequest): Validation {
  if (!request.intentConversion) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'intentConversion must not be empty');
  }
  if (!request.intentSummary) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'intentSummary must not be empty');
  }
  if (request.intentId !== undefined && request.intentId !== null && !request.intentId) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'intentId must not be empty when provided');
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
 * Validates the signature information of an intent level: `alg`,
 * `keyIdentifier`, and the signature must all be present. The field
 * names use the L1 numbering (`intentL1`), the L2/L3 validators pass
 * their own prefixes.
 */
function validateSignatureInfo(info: SignatureInfo, field: string): Validation {
  if (!info) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, field + '.sign must not be null');
  }
  if (!info.alg) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, field + '.sign.alg must not be empty');
  }
  if (!info.keyIdentifier) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, field + '.sign.keyIdentifier must not be empty');
  }
  if (!info.sign) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, field + '.sign.sign must not be empty');
  }
  return null;
}

/**
 * Validates an IntentL1 object structurally.
 * @param intent - The Level 1 intent to validate
 * @returns A validation failure if invalid, or null if valid
 */
export function validateIntentL1(intent: IntentL1): Validation {
  if (!intent.version) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'intentL1.version must not be empty');
  }
  if (!intent.intentId) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'intentL1.intentId must not be empty');
  }
  if (!intent.intentConversion) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'intentL1.intentConversion must not be empty');
  }
  if (!intent.intentSummary) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'intentL1.intentSummary must not be empty');
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
  if (!intent.version) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'intentL2.version must not be empty');
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
  if (!intent.version) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'intentL3.version must not be empty');
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
  const signErr = validateSignatureInfo(intent.sign, 'intentL3');
  if (signErr) {
    return signErr;
  }
  return null;
}
