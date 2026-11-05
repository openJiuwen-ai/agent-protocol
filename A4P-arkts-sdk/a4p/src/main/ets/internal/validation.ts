/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

import { CreateIntentRequest, IntentIssueContext, A4pErrorCode, PROTOCOL_VERSION } from '../models/common';
import { PaymentConstraints } from '../models/constraint';
import { StructuredIntent, IntentActionType, AlgorithmType, IntentL1, IntentL2, IntentL3, SignatureInfo } from '../models/intent';
import { UserIdentity, AgentIdentity, ServiceProviderIdentity, SigningEnv } from '../models/identity';
import {
  Execution, Order, IntentExecutionContext, ExecutionRecord, PaymentExecutionInfo,
  IntentExecutionStatus, ExecutionStatus
} from '../models/execution';
import { Product, ItemWhiteList, ShoppingItems, CartItem } from '../models/product';
import {
  PeriodRule, periodRuleOf, computeCurrentPeriodIndex, computeExecutionCount,
  computeAccumulatedAmount, computePeriodExecutionCount
} from './accounting';

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
 * Whether an optional protocol field carries a value. Null and undefined
 * both mean absent: a null optional field is omitted from the canonical
 * form rather than spelled "key":null, so a present null is not a
 * malformed value; it is simply not there.
 */
function isPresent(v: number | string | boolean | object | undefined): boolean {
  return v !== undefined && v !== null;
}

/**
 * The allowed property names of each protocol object. A property outside
 * its object's set is an unknown field: an extra property would build an
 * intent no protocol peer accepts, and a deserialized document no
 * serializer could have produced. Fail closed with A4P_INVALID_PARAMETER.
 */
const SELLER_FIELDS: string[] = ['id'];
const SPUSKU_FIELDS: string[] = ['spuId', 'skuId'];
const PRODUCT_FIELDS: string[] = ['seller', 'spusku'];
const ITEM_WHITE_LIST_FIELDS: string[] = ['whiteList', 'quantity'];
const SHOPPING_ITEMS_FIELDS: string[] = ['itemsWhiteList'];
const CART_ITEM_FIELDS: string[] = ['product', 'quantity'];
const CONSTRAINTS_FIELDS: string[] = ['singleOrderMaxAmount', 'totalMaxAmount', 'currency', 'validFrom',
  'validUntil', 'maxExecutionCount', 'anchorTime', 'periodSeconds', 'maxExecutionCountPerPeriod'];
const ORDER_FIELDS: string[] = ['orderId', 'amount', 'currency', 'cartItems', 'timestamp'];
const STRUCTURED_INTENT_FIELDS: string[] = ['action', 'resources', 'constraints'];
const SIGNATURE_INFO_FIELDS: string[] = ['alg', 'keyIdentifier', 'sign'];
const USER_IDENTITY_FIELDS: string[] = ['identityProvider', 'id', 'additionalInfo'];
const AGENT_IDENTITY_FIELDS: string[] = ['identityProvider', 'id', 'additionalInfo'];
const SERVICE_PROVIDER_FIELDS: string[] = ['id', 'additionalInfo'];
const SIGNING_ENV_FIELDS: string[] = ['device', 'app'];
const L1_FIELDS: string[] = ['version', 'intentId', 'intentConversation', 'intentSummary',
  'structuredIntent', 'timestamp', 'sign'];
const L2_FIELDS: string[] = ['version', 'intentL1', 'userIdentity', 'agentIdentity',
  'serviceProviderIdentity', 'timestamp', 'sign'];
const L3_FIELDS: string[] = ['version', 'intentL2', 'signingEnv', 'timestamp', 'sign'];
const EXECUTION_FIELDS: string[] = ['action', 'id', 'executionContent'];
const RECORD_FIELDS: string[] = ['executionId', 'status', 'action', 'executionContent',
  'periodIndex', 'reservedAt', 'completedAt', 'canceledAt'];
const EXECUTION_INFO_FIELDS: string[] = ['periodIndex', 'leftPeriodExecutionCount',
  'leftExecutionCount', 'leftAmount'];
const CONTEXT_FIELDS: string[] = ['version', 'intentId', 'authorizedIntent', 'status',
  'activatedTime', 'revokedTime', 'executions', 'executionInfo'];
const REQUEST_FIELDS: string[] = ['intentConversation', 'intentSummary', 'structuredIntent', 'intentId'];
const ISSUE_CONTEXT_FIELDS: string[] = ['userIdentity', 'agentIdentity', 'serviceProviderIdentity'];

/**
 * Rejects properties outside the object's allowed set. Shared by the
 * deserialization path and the in-memory API path: an object carrying
 * extra properties is not a protocol object either way.
 * @param obj - The protocol object whose property names to check
 * @param allowed - The allowed property names for this object kind
 * @param field - The field path prefix for error messages
 * @returns A validation failure naming the first unknown field, or null
 */
function rejectUnknownFields(obj: object, allowed: string[], field: string): Validation {
  const keys: string[] = Object.keys(obj);
  for (let i = 0; i < keys.length; i++) {
    if (allowed.indexOf(keys[i]) < 0) {
      return fail(A4pErrorCode.A4P_INVALID_PARAMETER,
        field + ' carries an unknown field \'' + keys[i] + '\' (allowed: ' + allowed.join(', ') + ')');
    }
  }
  return null;
}

/**
 * Whether a signing-environment value tree contains a null anywhere. The
 * canonical form has no nulls (serialization omits them), so a null inside
 * device/app would be silently dropped from the signed bytes — reject it
 * loudly instead of signing bytes different from what was validated.
 */
function containsNullValue(value: Object): boolean {
  if (value === null || value === undefined) {
    return true;
  }
  if (Array.isArray(value)) {
    for (let i = 0; i < value.length; i++) {
      if (containsNullValue(value[i] as Object)) {
        return true;
      }
    }
    return false;
  }
  if (typeof value === 'object') {
    const keys: string[] = Object.keys(value as Record<string, Object>);
    for (let i = 0; i < keys.length; i++) {
      if (containsNullValue((value as Record<string, Object>)[keys[i]])) {
        return true;
      }
    }
  }
  return false;
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
 * period rule. Optional caps (`singleOrderMaxAmount`, `maxExecutionCount`)
 * are validated only when present; the period rule is all-or-nothing —
 * all three fields provided (periodic) or all three omitted
 * (non-periodic), never partial.
 */
function validatePaymentConstraints(pc: PaymentConstraints): Validation {
  const fieldsErr = rejectUnknownFields(pc, CONSTRAINTS_FIELDS, 'structuredIntent.constraints');
  if (fieldsErr) {
    return fieldsErr;
  }
  if (isPresent(pc.singleOrderMaxAmount) && !isPositiveInteger(pc.singleOrderMaxAmount)) {
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
  if (isPresent(pc.maxExecutionCount) && !isPositiveInteger(pc.maxExecutionCount)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'maxExecutionCount must be a positive integer');
  }
  const hasAnchorTime: boolean = isPresent(pc.anchorTime);
  const hasPeriodSeconds: boolean = isPresent(pc.periodSeconds);
  const hasPerPeriodCount: boolean = isPresent(pc.maxExecutionCountPerPeriod);
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
  const fieldsErr = rejectUnknownFields(product, PRODUCT_FIELDS, field);
  if (fieldsErr) {
    return fieldsErr;
  }
  if (!product.seller) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, field + '.seller must not be null');
  }
  const sellerErr = rejectUnknownFields(product.seller, SELLER_FIELDS, field + '.seller');
  if (sellerErr) {
    return sellerErr;
  }
  if (!isNonBlankString(product.seller.id)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, field + '.seller.id must not be blank');
  }
  if (!product.spusku) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, field + '.spusku must not be null');
  }
  const spuskuErr = rejectUnknownFields(product.spusku, SPUSKU_FIELDS, field + '.spusku');
  if (spuskuErr) {
    return spuskuErr;
  }
  if (isPresent(product.spusku.spuId) && !isNonBlankString(product.spusku.spuId)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, field + '.spusku.spuId must not be blank when provided');
  }
  if (isPresent(product.spusku.skuId) && !isNonBlankString(product.spusku.skuId)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, field + '.spusku.skuId must not be blank when provided');
  }
  if (!isPresent(product.spusku.spuId) && !isPresent(product.spusku.skuId)) {
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
  const fieldsErr = rejectUnknownFields(group, ITEM_WHITE_LIST_FIELDS, field);
  if (fieldsErr) {
    return fieldsErr;
  }
  // The shape is checked before the emptiness: a truthy non-array (e.g. an
  // object literal) has no length to compare and would otherwise pass the
  // emptiness test, then break the callers that iterate the list.
  if (!Array.isArray(group.whiteList) || group.whiteList.length === 0) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, field + '.whiteList must be a non-empty array');
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
 * Validates the resources: a non-empty list of valid whitelist groups with
 * no product shared by two groups. The resources are an order template and
 * a cart item finds its group by product alone, so a product must
 * uniquely determine its group.
 */
function validateShoppingItems(resources: ShoppingItems, field: string): Validation {
  const fieldsErr = rejectUnknownFields(resources, SHOPPING_ITEMS_FIELDS, field);
  if (fieldsErr) {
    return fieldsErr;
  }
  if (!Array.isArray(resources.itemsWhiteList) || resources.itemsWhiteList.length === 0) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, field + '.itemsWhiteList must be a non-empty array');
  }
  for (let i = 0; i < resources.itemsWhiteList.length; i++) {
    const groupErr = validateItemWhiteList(resources.itemsWhiteList[i], field + '.itemsWhiteList[' + i + ']');
    if (groupErr) {
      return groupErr;
    }
    for (let j = 0; j < i; j++) {
      const earlier: ItemWhiteList = resources.itemsWhiteList[j];
      const current: ItemWhiteList = resources.itemsWhiteList[i];
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
  if (!Array.isArray(cartItems) || cartItems.length === 0) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, field + ' must be a non-empty array');
  }
  for (let i = 0; i < cartItems.length; i++) {
    const item = cartItems[i];
    if (!item) {
      return fail(A4pErrorCode.A4P_INVALID_PARAMETER, field + '[' + i + '] must not be null');
    }
    const fieldsErr = rejectUnknownFields(item, CART_ITEM_FIELDS, field + '[' + i + ']');
    if (fieldsErr) {
      return fieldsErr;
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

/** Validates a StructuredIntent: action, resources, and constraints. */
function validateStructuredIntent(si: StructuredIntent): Validation {
  const fieldsErr = rejectUnknownFields(si, STRUCTURED_INTENT_FIELDS, 'structuredIntent');
  if (fieldsErr) {
    return fieldsErr;
  }
  if (!isValidIntentActionType(si.action)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'structuredIntent.action must be a valid IntentActionType');
  }
  if (!si.resources) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'structuredIntent.resources must not be null');
  }
  const resourcesErr = validateShoppingItems(si.resources, 'structuredIntent.resources');
  if (resourcesErr) {
    return resourcesErr;
  }
  if (!si.constraints) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'structuredIntent.constraints must not be null');
  }
  return validatePaymentConstraints(si.constraints);
}

/** Validates a UserIdentity: non-empty provider and id, string additionalInfo. */
function validateUserIdentity(ui: UserIdentity): Validation {
  const fieldsErr = rejectUnknownFields(ui, USER_IDENTITY_FIELDS, 'userIdentity');
  if (fieldsErr) {
    return fieldsErr;
  }
  if (!isNonBlankString(ui.identityProvider)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'userIdentity.identityProvider must not be blank');
  }
  if (!isNonBlankString(ui.id)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'userIdentity.id must not be blank');
  }
  // A non-string additionalInfo (a number, say) must fail loudly here
  // rather than pass validation and produce non-canonical bytes.
  if (isPresent(ui.additionalInfo) && typeof ui.additionalInfo !== 'string') {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'userIdentity.additionalInfo must be a string when provided');
  }
  return null;
}

/** Validates an AgentIdentity: non-empty provider and id, string additionalInfo. */
function validateAgentIdentity(ai: AgentIdentity): Validation {
  const fieldsErr = rejectUnknownFields(ai, AGENT_IDENTITY_FIELDS, 'agentIdentity');
  if (fieldsErr) {
    return fieldsErr;
  }
  if (!isNonBlankString(ai.identityProvider)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'agentIdentity.identityProvider must not be blank');
  }
  if (!isNonBlankString(ai.id)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'agentIdentity.id must not be blank');
  }
  if (isPresent(ai.additionalInfo) && typeof ai.additionalInfo !== 'string') {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'agentIdentity.additionalInfo must be a string when provided');
  }
  return null;
}

/** Validates a ServiceProviderIdentity: non-empty id, string additionalInfo. */
function validateServiceProviderIdentity(spi: ServiceProviderIdentity): Validation {
  const fieldsErr = rejectUnknownFields(spi, SERVICE_PROVIDER_FIELDS, 'serviceProviderIdentity');
  if (fieldsErr) {
    return fieldsErr;
  }
  if (!isNonBlankString(spi.id)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'serviceProviderIdentity.id must not be blank');
  }
  if (isPresent(spi.additionalInfo) && typeof spi.additionalInfo !== 'string') {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER,
      'serviceProviderIdentity.additionalInfo must be a string when provided');
  }
  return null;
}

/**
 * Validates a SigningEnv object: present device and app maps, each an
 * object (not an array or scalar) free of null values — the canonical form
 * omits nulls, so a null inside them would be dropped from the signed
 * bytes rather than represented, and no peer renders it back.
 * @param env - The signing environment to validate
 * @returns A validation failure if invalid, or null if valid
 */
export function validateSigningEnv(env: SigningEnv): Validation {
  if (!env) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'signingEnv must not be null');
  }
  const fieldsErr = rejectUnknownFields(env, SIGNING_ENV_FIELDS, 'signingEnv');
  if (fieldsErr) {
    return fieldsErr;
  }
  if (!env.device || typeof env.device !== 'object' || Array.isArray(env.device)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'signingEnv.device must be an object');
  }
  if (containsNullValue(env.device)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER,
      'signingEnv.device must not contain null values; omit the key instead');
  }
  if (!env.app || typeof env.app !== 'object' || Array.isArray(env.app)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'signingEnv.app must be an object');
  }
  if (containsNullValue(env.app)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER,
      'signingEnv.app must not contain null values; omit the key instead');
  }
  return null;
}

/** Validates an Order: non-empty id and currency, positive amount, valid cart, timestamp. */
function validateOrder(order: Order): Validation {
  if (!order) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'order must not be null');
  }
  const fieldsErr = rejectUnknownFields(order, ORDER_FIELDS, 'order');
  if (fieldsErr) {
    return fieldsErr;
  }
  if (!isNonBlankString(order.orderId)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'order.orderId must not be blank');
  }
  if (!isPositiveInteger(order.amount)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'order.amount must be a positive integer');
  }
  if (!isNonBlankString(order.currency)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'order.currency must not be blank');
  }
  if (!Array.isArray(order.cartItems) || order.cartItems.length === 0) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'order.cartItems must be a non-empty array');
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
 * Validates an Execution object: a defined action, an optional non-blank id
 * (null counts as absent — an absent id is generated during reservation),
 * and an executionContent that is a well-formed order.
 * @param exec - The execution to validate
 * @returns A validation failure if invalid, or null if valid
 */
export function validateExecution(exec: Execution): Validation {
  if (!exec) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'execution must not be null');
  }
  const fieldsErr = rejectUnknownFields(exec, EXECUTION_FIELDS, 'execution');
  if (fieldsErr) {
    return fieldsErr;
  }
  if (!isValidIntentActionType(exec.action)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'execution.action must be a valid IntentActionType');
  }
  if (isPresent(exec.id) && !isNonBlankString(exec.id)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER,
      'execution.id must be a non-blank string when provided (an absent id is generated)');
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
  if (!request) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'request must not be null');
  }
  const fieldsErr = rejectUnknownFields(request, REQUEST_FIELDS, 'request');
  if (fieldsErr) {
    return fieldsErr;
  }
  if (!isNonBlankString(request.intentConversation)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'intentConversation must not be blank');
  }
  if (!isNonBlankString(request.intentSummary)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'intentSummary must not be blank');
  }
  if (isPresent(request.intentId) && !isNonBlankString(request.intentId)) {
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
  const fieldsErr = rejectUnknownFields(ctx, ISSUE_CONTEXT_FIELDS, 'issueContext');
  if (fieldsErr) {
    return fieldsErr;
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
  const fieldsErr = rejectUnknownFields(info, SIGNATURE_INFO_FIELDS, field + '.sign');
  if (fieldsErr) {
    return fieldsErr;
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
  const fieldsErr = rejectUnknownFields(intent, L1_FIELDS, 'intentL1');
  if (fieldsErr) {
    return fieldsErr;
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
  const fieldsErr = rejectUnknownFields(intent, L2_FIELDS, 'intentL2');
  if (fieldsErr) {
    return fieldsErr;
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
  const fieldsErr = rejectUnknownFields(intent, L3_FIELDS, 'intentL3');
  if (fieldsErr) {
    return fieldsErr;
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
 * one — and only there), and the flat reserved-execution fields (a valid
 * action and a well-formed executionContent).
 * @param record - The execution record to validate
 * @param field - The field path prefix for error messages
 * @returns A validation failure if invalid, or null if valid
 */
function validateExecutionRecord(record: ExecutionRecord, field: string): Validation {
  if (!record) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, field + ' must not be null');
  }
  const fieldsErr = rejectUnknownFields(record, RECORD_FIELDS, field);
  if (fieldsErr) {
    return fieldsErr;
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
  if (isPresent(record.periodIndex) && !isNonNegativeInteger(record.periodIndex)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER,
      field + '.periodIndex must be a non-negative integer when present');
  }
  if (!isNonNegativeInteger(record.reservedAt)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, field + '.reservedAt must be a non-negative integer');
  }
  if (isPresent(record.completedAt) && !isNonNegativeInteger(record.completedAt)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER,
      field + '.completedAt must be a non-negative integer when present');
  }
  if (isPresent(record.canceledAt) && !isNonNegativeInteger(record.canceledAt)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER,
      field + '.canceledAt must be a non-negative integer when present');
  }
  // Finalization timestamps are status-bound: completeExecution stamps
  // completedAt, cancelExecution stamps canceledAt, and no other
  // transition exists, so any other pairing is corrupted data.
  if (record.status === ExecutionStatus.COMPLETED) {
    if (!isPresent(record.completedAt)) {
      return fail(A4pErrorCode.A4P_INVALID_PARAMETER,
        field + '.completedAt must be present on a COMPLETED record');
    }
  } else if (isPresent(record.completedAt)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER,
      field + '.completedAt must be present only on a COMPLETED record');
  }
  if (record.status === ExecutionStatus.CANCELED) {
    if (!isPresent(record.canceledAt)) {
      return fail(A4pErrorCode.A4P_INVALID_PARAMETER,
        field + '.canceledAt must be present on a CANCELED record');
    }
  } else if (isPresent(record.canceledAt)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER,
      field + '.canceledAt must be present only on a CANCELED record');
  }
  // Record timestamps are monotonic: a finalization can
  // never predate the reservation it finalizes (completeExecution and
  // cancelExecution reject such a clock), so a persisted record violating
  // it is corrupted data and fails with the code the offending operation
  // would have returned.
  if (isPresent(record.completedAt) && record.completedAt < record.reservedAt) {
    return fail(A4pErrorCode.A4P_TIMESTAMP_TOO_EARLY,
      field + '.completedAt must not be earlier than ' + field + '.reservedAt');
  }
  if (isPresent(record.canceledAt) && record.canceledAt < record.reservedAt) {
    return fail(A4pErrorCode.A4P_TIMESTAMP_TOO_EARLY,
      field + '.canceledAt must not be earlier than ' + field + '.reservedAt');
  }
  // The reserved execution's fields live flat on the record: a valid
  // action and a well-formed order, exactly like the execution
  // createExecution received and validated the same way.
  if (!isValidIntentActionType(record.action)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, field + '.action must be a valid IntentActionType');
  }
  if (!record.executionContent) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, field + '.executionContent must not be null');
  }
  const orderErr = validateOrder(record.executionContent);
  if (orderErr) {
    return orderErr;
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
  const fieldsErr = rejectUnknownFields(info, EXECUTION_INFO_FIELDS, field);
  if (fieldsErr) {
    return fieldsErr;
  }
  // The snapshot period index may be negative: activateIntent judges only
  // the window's upper bound (validUntil) and permits an instant before
  // validFrom, so an activation before the anchor snapshots the pre-anchor
  // period. Record indexes, in contrast, are reserved at
  // now >= validFrom >= anchorTime and are never negative.
  if (isPresent(info.periodIndex) && !isSafeInteger(info.periodIndex)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER,
      field + '.periodIndex must be a safe integer when present');
  }
  if (isPresent(info.leftPeriodExecutionCount) && !isNonNegativeInteger(info.leftPeriodExecutionCount)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER,
      field + '.leftPeriodExecutionCount must be a non-negative integer when present');
  }
  if (isPresent(info.leftExecutionCount) && !isNonNegativeInteger(info.leftExecutionCount)) {
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
 * a defined lifecycle status, the activation time, the status-bound
 * revokedTime (present exactly when status is REVOKED, and never before
 * activatedTime), every ledger record, and the accounting
 * snapshot — including the cap-tied presence rules: records carry a
 * periodIndex exactly when the intent is periodic, and the snapshot
 * carries its period figures exactly when periodic and
 * leftExecutionCount exactly when the intent has a maxExecutionCount cap.
 * Used both by the codec when deserializing a context (see
 * A4pCodec.deserializeIntentExecutionContext) and by the execution APIs
 * (revoke/create/complete/cancel): the caller's context is untrusted
 * in-memory state whose fields the APIs dereference directly, so a
 * malformed one must surface as a validation failure — an A4pError —
 * instead of a raw TypeError.
 * @param context - The execution context to validate
 * @returns A validation failure if invalid, or null if valid
 */
export function validateIntentExecutionContext(context: IntentExecutionContext): Validation {
  if (!context) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'context must not be null');
  }
  const fieldsErr = rejectUnknownFields(context, CONTEXT_FIELDS, 'context');
  if (fieldsErr) {
    return fieldsErr;
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
  if (!isNonNegativeInteger(context.activatedTime)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'context.activatedTime must be a non-negative integer');
  }
  // revokedTime is status-bound, like the records' finalization stamps:
  // revokeIntent stamps it, and no other transition flips to REVOKED.
  if (isPresent(context.revokedTime) && !isNonNegativeInteger(context.revokedTime)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER,
      'context.revokedTime must be a non-negative integer when present');
  }
  if (context.status === IntentExecutionStatus.REVOKED) {
    if (!isPresent(context.revokedTime)) {
      return fail(A4pErrorCode.A4P_INVALID_PARAMETER,
        'context.revokedTime must be present when status is REVOKED');
    }
  } else if (isPresent(context.revokedTime)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER,
      'context.revokedTime must be present only when status is REVOKED');
  }
  // A revocation can never predate the activation it revokes (revokeIntent
  // rejects such a clock), so a context violating it is corrupted data and
  // fails with the code the offending operation would have returned.
  if (isPresent(context.revokedTime) && context.revokedTime < context.activatedTime) {
    return fail(A4pErrorCode.A4P_TIMESTAMP_TOO_EARLY,
      'context.revokedTime must not be earlier than context.activatedTime');
  }
  // The ledger may be empty (nothing reserved yet) but must be an array.
  if (!Array.isArray(context.executions)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'context.executions must be an array');
  }
  // The period rule is all-or-nothing (validated with the intent chain),
  // so the anchor alone decides periodicity. A null anchor counts as
  // absent: in-memory null is the protocol's spelling of an omitted
  // optional field.
  const periodic: boolean = isPresent(l1.structuredIntent.constraints.anchorTime);
  for (let i = 0; i < context.executions.length; i++) {
    const recordErr = validateExecutionRecord(context.executions[i], 'context.executions[' + i + ']');
    if (recordErr) {
      return recordErr;
    }
    if (periodic && !isPresent(context.executions[i].periodIndex)) {
      return fail(A4pErrorCode.A4P_INVALID_PARAMETER,
        'context.executions[' + i + '].periodIndex must be present on a periodic intent');
    }
    if (!periodic && isPresent(context.executions[i].periodIndex)) {
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
  if (periodic && (!isPresent(context.executionInfo.periodIndex) ||
    !isPresent(context.executionInfo.leftPeriodExecutionCount))) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER,
      'context.executionInfo.periodIndex and leftPeriodExecutionCount must be present on a periodic intent');
  }
  if (!periodic && (isPresent(context.executionInfo.periodIndex) ||
    isPresent(context.executionInfo.leftPeriodExecutionCount))) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER,
      'context.executionInfo.periodIndex and leftPeriodExecutionCount must be absent on a non-periodic intent');
  }
  // Normalize a null cap to undefined so the presence rules below (and the
  // accounting math the callers run) treat it exactly like an absent cap —
  // otherwise `cap === undefined` would be false for null and the
  // comparisons would coerce null to 0.
  const countCap: number | undefined = l1.structuredIntent.constraints.maxExecutionCount ?? undefined;
  if (countCap !== undefined && !isPresent(context.executionInfo.leftExecutionCount)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER,
      'context.executionInfo.leftExecutionCount must be present when the intent carries maxExecutionCount');
  }
  if (countCap === undefined && isPresent(context.executionInfo.leftExecutionCount)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER,
      'context.executionInfo.leftExecutionCount must be absent when the intent carries no maxExecutionCount');
  }
  return null;
}

/**
 * Checks that the intent chain carries the protocol version at every
 * level. A mixed-version chain is signature-valid but semantically
 * unsupported and must be rejected.
 * @param intentL3 - The Level 3 intent chain to check
 * @returns A validation failure if any version mismatches, or null if all match
 */
export function checkChainVersions(intentL3: IntentL3): A4pValidationFailure | null {
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
 * Enforces the authorization scope the user actually signed: every cart
 * product must be on at least one whitelist group of the resource.
 * @param context - The execution context carrying the authorized intent
 * @param cartItems - The ordered cart items to validate against the authorized scope
 * @returns A validation failure if the cart exceeds scope, or null if within scope
 */
export function enforceWhitelistScope(context: IntentExecutionContext,
                                      cartItems: CartItem[]): A4pValidationFailure | null {
  const resources: ShoppingItems = context.authorizedIntent.intentL2.intentL1.structuredIntent.resources;
  for (let i = 0; i < cartItems.length; i++) {
    const item = cartItems[i];
    const whitelisted: boolean = resources.itemsWhiteList.some(
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
export function enforceTemplateMatch(context: IntentExecutionContext,
                                     cartItems: CartItem[]): A4pValidationFailure | null {
  const groups: ItemWhiteList[] =
    context.authorizedIntent.intentL2.intentL1.structuredIntent.resources.itemsWhiteList;
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
 * Validates the content of a restored execution context: the persisted
 * ledger is untrusted, so it must be one the execution operations could
 * have produced for the authorized intent, and a violation fails with
 * the code the offending operation would have returned.
 *
 * This is the restore-only pass on top of validateIntentExecutionContext
 * (which checks shape and closed field sets): it replays the
 * reachability rules the lifecycle APIs enforce — the activation
 * instant against the validity window (the chain's stamps carry no
 * cross-level ordering: each level's interface runs in a different
 * process, so only the window is judged), every
 * record's reservation instant against the activation floor and the
 * window, each record's period index against its own reservation
 * instant, execution/order id uniqueness, the per-record parameter
 * consistency with the signed snapshot (action, currency, single-order
 * cap, cart scope and template), the accumulated caps, the status
 * agreement for the clock-independent statuses, and the accounting
 * snapshot against a re-derivation from the ledger (evaluated at the
 * stored period index, since the instant of the last recompute is not
 * recorded).
 *
 * It is deliberately clock-free: EXPIRED is a lazy, now-dependent flip
 * and REVOKED is reachable with any ledger, so only ACTIVE/CONSUMED are
 * checked against the ledger. It does not verify signatures either —
 * callers wanting that guarantee run
 * verifyIntentAuthenticity(restored.authorizedIntent, verifier) after
 * the restore, and storage integrity (a consistent-but-tampered
 * ledger) remains the persistence layer's responsibility.
 *
 * @param context - The shape-validated restored context to check
 * @returns A validation failure describing the first unreachable element, or null when reachable
 */
export function validateRestoredContext(context: IntentExecutionContext): Validation {
  // The context carries the protocol version like every intent level;
  // one from a different protocol generation is unsupported data.
  if (context.version !== PROTOCOL_VERSION) {
    return fail(A4pErrorCode.A4P_UNSUPPORTED_VERSION,
      'Context version ' + context.version + ' is not supported');
  }
  const versionErr: A4pValidationFailure | null = checkChainVersions(context.authorizedIntent);
  if (versionErr) {
    return versionErr;
  }
  const l1: IntentL1 = context.authorizedIntent.intentL2.intentL1;
  const constraints: PaymentConstraints = l1.structuredIntent.constraints;
  // activateIntent's window rule, boundary included: an activation
  // exactly at validUntil is legal and must keep round-tripping. The
  // chain's stamps carry no cross-level ordering (each level's interface
  // runs in a different process), so only the window is judged.
  if (context.activatedTime > constraints.validUntil) {
    return fail(A4pErrorCode.A4P_TIMESTAMP_EXPIRED,
      'context.activatedTime ' + context.activatedTime + ' is after validUntil ' + constraints.validUntil);
  }
  const rule: PeriodRule | null = periodRuleOf(constraints);
  const executionIds: Set<string> = new Set<string>();
  const orderIds: Set<string> = new Set<string>();
  const periodCounts: Map<number, number> = new Map<number, number>();
  for (let i = 0; i < context.executions.length; i++) {
    const record: ExecutionRecord = context.executions[i];
    const field: string = 'context.executions[' + i + ']';
    // createExecution's floors and ceiling, replayed against the
    // record's own reservation instant.
    if (record.reservedAt < context.activatedTime) {
      return fail(A4pErrorCode.A4P_INVALID_PARAMETER,
        field + '.reservedAt ' + record.reservedAt + ' is earlier than context.activatedTime ' +
        context.activatedTime);
    }
    const order: Order = record.executionContent;
    // Parameter consistency precedes the window replay, matching the
    // Java ledger order and createExecution's live order.
    if (record.action !== l1.structuredIntent.action) {
      return fail(A4pErrorCode.A4P_INVALID_PARAMETER,
        field + '.action ' + record.action + ' does not match the authorized intent action ' +
        l1.structuredIntent.action);
    }
    if (order.currency !== constraints.currency) {
      return fail(A4pErrorCode.A4P_INVALID_PARAMETER,
        'Currency mismatch: order is ' + order.currency + ', intent constraint is ' + constraints.currency);
    }
    if (record.reservedAt < constraints.validFrom) {
      return fail(A4pErrorCode.A4P_TIMESTAMP_TOO_EARLY,
        field + '.reservedAt ' + record.reservedAt + ' is before validFrom ' + constraints.validFrom);
    }
    if (record.reservedAt > constraints.validUntil) {
      return fail(A4pErrorCode.A4P_TIMESTAMP_EXPIRED,
        field + '.reservedAt ' + record.reservedAt + ' is after validUntil ' + constraints.validUntil);
    }
    if (rule !== null) {
      // The period index is not free data: it is the period containing
      // the record's own reservation instant.
      const expectedPeriod: number = computeCurrentPeriodIndex(rule, record.reservedAt);
      if (record.periodIndex !== expectedPeriod) {
        return fail(A4pErrorCode.A4P_INVALID_PARAMETER,
          field + '.periodIndex ' + record.periodIndex + ' does not match the period of its reservedAt ' +
          record.reservedAt + ' (expected ' + expectedPeriod + ')');
      }
    }
    if (executionIds.has(record.executionId)) {
      return fail(A4pErrorCode.A4P_DUPLICATE_ID,
        field + '.executionId is already in the ledger: ' + record.executionId);
    }
    executionIds.add(record.executionId);
    if (orderIds.has(order.orderId)) {
      return fail(A4pErrorCode.A4P_DUPLICATE_ID,
        field + ' carries an orderId already in the ledger: ' + order.orderId);
    }
    orderIds.add(order.orderId);
    const singleOrderCap: number | undefined = constraints.singleOrderMaxAmount ?? undefined;
    if (singleOrderCap !== undefined && order.amount > singleOrderCap) {
      return fail(A4pErrorCode.A4P_SINGLE_ORDER_AMOUNT_EXCEEDED,
        field + ' order amount ' + order.amount + ' exceeds singleOrderMaxAmount ' + singleOrderCap);
    }
    if (l1.structuredIntent.action === IntentActionType.AI_PAY) {
      const scopeErr: A4pValidationFailure | null = enforceWhitelistScope(context, order.cartItems);
      if (scopeErr) {
        return scopeErr;
      }
      const templateErr: A4pValidationFailure | null = enforceTemplateMatch(context, order.cartItems);
      if (templateErr) {
        return templateErr;
      }
    }
    const period: number | undefined = record.periodIndex;
    if (rule !== null && record.status !== ExecutionStatus.CANCELED && period !== undefined) {
      const count: number = (periodCounts.get(period) ?? 0) + 1;
      periodCounts.set(period, count);
    }
  }
  // The accumulated caps createExecution enforces before appending; a
  // ledger already over them is not one it could have produced.
  const countCap: number | undefined = constraints.maxExecutionCount ?? undefined;
  if (countCap !== undefined) {
    const executionCount: number = computeExecutionCount(context.executions);
    if (executionCount > countCap) {
      return fail(A4pErrorCode.A4P_EXECUTION_COUNT_EXCEEDED,
        'Ledger execution count ' + executionCount + ' exceeds maxExecutionCount ' + countCap);
    }
  }
  const accumulatedAmount: number = computeAccumulatedAmount(context.executions);
  if (accumulatedAmount > constraints.totalMaxAmount) {
    return fail(A4pErrorCode.A4P_AMOUNT_EXCEEDED,
      'Ledger accumulated amount ' + accumulatedAmount + ' exceeds totalMaxAmount ' +
      constraints.totalMaxAmount);
  }
  if (rule !== null) {
    const periodCap: number = rule.maxExecutionCountPerPeriod;
    for (let i = 0; i < context.executions.length; i++) {
      const record: ExecutionRecord = context.executions[i];
      const period: number | undefined = record.periodIndex;
      if (record.status !== ExecutionStatus.CANCELED && period !== undefined) {
        const count: number = periodCounts.get(period) ?? 0;
        if (count > periodCap) {
          return fail(A4pErrorCode.A4P_PERIOD_EXECUTION_COUNT_EXCEEDED,
            'Ledger period ' + period + ' execution count ' + count +
            ' exceeds maxExecutionCountPerPeriod ' + periodCap);
        }
      }
    }
  }
  // completeExecution's CONSUMED flip is deterministic given the ledger,
  // so an ACTIVE/CONSUMED status must agree with it. EXPIRED (a lazy,
  // now-dependent flip) and REVOKED are reachable with any ledger.
  if (context.status === IntentExecutionStatus.ACTIVE || context.status === IntentExecutionStatus.CONSUMED) {
    const allDone: boolean = context.executions.every(
      (r: ExecutionRecord) => r.status === ExecutionStatus.COMPLETED || r.status === ExecutionStatus.CANCELED);
    const countReached: boolean = countCap !== undefined &&
      computeExecutionCount(context.executions) >= countCap;
    const amountExhausted: boolean = accumulatedAmount >= constraints.totalMaxAmount;
    const exhausted: boolean = allDone && (countReached || amountExhausted);
    if ((context.status === IntentExecutionStatus.CONSUMED) !== exhausted) {
      return fail(A4pErrorCode.A4P_INVALID_PARAMETER,
        'context.status ' + context.status + ' is inconsistent with the restored ledger: ' +
        (exhausted ? 'a fully finalized, exhausted ledger yields CONSUMED'
          : 'an unexhausted or still-pending ledger yields ACTIVE'));
    }
  }
  // executionInfo is derived state: it must equal what the restored
  // ledger yields, evaluated at the stored period (the instant of the
  // last recompute is not recorded, so the stored index is the
  // evaluation period by necessity).
  const expectedLeftAmount: number = constraints.totalMaxAmount - accumulatedAmount;
  if (context.executionInfo.leftAmount !== expectedLeftAmount) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER,
      'context.executionInfo.leftAmount ' + context.executionInfo.leftAmount +
      ' does not match the ledger (expected ' + expectedLeftAmount + ')');
  }
  if (countCap !== undefined) {
    const expectedLeftCount: number = countCap - computeExecutionCount(context.executions);
    if (context.executionInfo.leftExecutionCount !== expectedLeftCount) {
      return fail(A4pErrorCode.A4P_INVALID_PARAMETER,
        'context.executionInfo.leftExecutionCount ' + context.executionInfo.leftExecutionCount +
        ' does not match the ledger (expected ' + expectedLeftCount + ')');
    }
  }
  if (rule !== null) {
    const storedPeriod: number | undefined = context.executionInfo.periodIndex;
    if (storedPeriod === undefined) {
      return fail(A4pErrorCode.A4P_INVALID_PARAMETER,
        'context.executionInfo.periodIndex must be present on a periodic intent');
    }
    const expectedLeftPeriod: number = rule.maxExecutionCountPerPeriod -
      computePeriodExecutionCount(context.executions, storedPeriod);
    if (context.executionInfo.leftPeriodExecutionCount !== expectedLeftPeriod) {
      return fail(A4pErrorCode.A4P_INVALID_PARAMETER,
        'context.executionInfo.leftPeriodExecutionCount ' + context.executionInfo.leftPeriodExecutionCount +
        ' does not match the ledger at the stored period ' + storedPeriod +
        ' (expected ' + expectedLeftPeriod + ')');
    }
  }
  return null;
}
