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

/**
 * Two validation layers share this file. The structure layer
 * (checkXxxStructure) judges whether an object is a well-formed protocol
 * document: closed field sets, required-field presence, per-field types
 * (strings, safe integers, arrays), enum membership, and status-bound
 * finalization presence. Deserialization runs exactly this layer (see
 * A4pCodec): the codec judges structure and types only, never parameter
 * values.
 *
 * The content layer (checkXxxContent) judges values — blankness, ranges,
 * ordering, duplicates, the period rule, and the ledger/context
 * consistency rules (intentId derivation, periodicity-bound presence,
 * monotonic finalization) — and runs only inside the composed validators
 * (validateXxx = structure, then content) that the lifecycle APIs use,
 * where an invalid parameter is A4P_INVALID_PARAMETER (1001): values
 * deserialization restores verbatim are judged by the APIs that operate
 * on the restored data. Content functions dereference freely — they run
 * only after the structure pass accepted the object.
 */

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

/** Whether v is a defined string (blankness is the content layer's isNonBlankString). */
function isString(v: string | undefined): boolean {
  return typeof v === 'string';
}

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

/** Structure pass for constraints: closed field set; every present figure is a safe integer. */
function checkPaymentConstraintsStructure(pc: PaymentConstraints): Validation {
  const fieldsErr = rejectUnknownFields(pc, CONSTRAINTS_FIELDS, 'structuredIntent.constraints');
  if (fieldsErr) {
    return fieldsErr;
  }
  if (pc.singleOrderMaxAmount !== undefined && !isSafeInteger(pc.singleOrderMaxAmount)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'singleOrderMaxAmount must be a safe integer when present');
  }
  if (!isSafeInteger(pc.totalMaxAmount)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'totalMaxAmount must be a safe integer');
  }
  if (!isString(pc.currency)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'currency must be a string');
  }
  if (!isSafeInteger(pc.validFrom)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'validFrom must be a safe integer');
  }
  if (!isSafeInteger(pc.validUntil)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'validUntil must be a safe integer');
  }
  if (pc.maxExecutionCount !== undefined && !isSafeInteger(pc.maxExecutionCount)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'maxExecutionCount must be a safe integer when present');
  }
  if (pc.anchorTime !== undefined && !isSafeInteger(pc.anchorTime)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'anchorTime must be a safe integer when present');
  }
  if (pc.periodSeconds !== undefined && !isSafeInteger(pc.periodSeconds)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'periodSeconds must be a safe integer when present');
  }
  if (pc.maxExecutionCountPerPeriod !== undefined && !isSafeInteger(pc.maxExecutionCountPerPeriod)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'maxExecutionCountPerPeriod must be a safe integer when present');
  }
  return null;
}

/**
 * Content pass for constraints: positive amounts and caps, non-blank
 * currency, ordered window, and the period rule all-or-nothing — all
 * three fields provided (periodic) or all three omitted, never partial.
 */
function checkPaymentConstraintsContent(pc: PaymentConstraints): Validation {
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
  if (pc.maxExecutionCount !== undefined && !isPositiveInteger(pc.maxExecutionCount)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'maxExecutionCount must be a positive integer');
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
    // An anchor later than validFrom would give pre-anchor reservations
    // negative period indexes, each with a fresh per-period budget.
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

/** Structure pass for a product (callers have rejected a null product): non-null seller/spusku, string ids, at least one of spuId/skuId. */
function checkProductStructure(product: Product, field: string): Validation {
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
  if (!isString(product.seller.id)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, field + '.seller.id must be a string');
  }
  if (!product.spusku) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, field + '.spusku must not be null');
  }
  const spuskuErr = rejectUnknownFields(product.spusku, SPUSKU_FIELDS, field + '.spusku');
  if (spuskuErr) {
    return spuskuErr;
  }
  if (product.spusku.spuId !== undefined && !isString(product.spusku.spuId)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, field + '.spusku.spuId must be a string when provided');
  }
  if (product.spusku.skuId !== undefined && !isString(product.spusku.skuId)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, field + '.spusku.skuId must be a string when provided');
  }
  if (product.spusku.spuId === undefined && product.spusku.skuId === undefined) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER,
      field + '.spusku must carry at least one of spuId or skuId');
  }
  return null;
}

/** Content pass for a product: non-blank seller id, and spuId/skuId when provided (see productMatches for why blank ids hollow out the scope). */
function checkProductContent(product: Product, field: string): Validation {
  if (!isNonBlankString(product.seller.id)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, field + '.seller.id must not be blank');
  }
  if (product.spusku.spuId !== undefined && !isNonBlankString(product.spusku.spuId)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, field + '.spusku.spuId must not be blank when provided');
  }
  if (product.spusku.skuId !== undefined && !isNonBlankString(product.spusku.skuId)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, field + '.spusku.skuId must not be blank when provided');
  }
  return null;
}

/** Structure pass for a whitelist group: whiteList array of non-null valid products, safe-integer quantity. */
function checkItemWhiteListStructure(group: ItemWhiteList, field: string): Validation {
  if (!group) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, field + ' must not be null');
  }
  const fieldsErr = rejectUnknownFields(group, ITEM_WHITE_LIST_FIELDS, field);
  if (fieldsErr) {
    return fieldsErr;
  }
  // The shape is checked before the emptiness: a truthy non-array (e.g. an
  // object literal) has no length to compare and would otherwise break
  // the callers that iterate the list.
  if (!Array.isArray(group.whiteList)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, field + '.whiteList must be an array');
  }
  for (let i = 0; i < group.whiteList.length; i++) {
    const product = group.whiteList[i];
    if (!product) {
      return fail(A4pErrorCode.A4P_INVALID_PARAMETER, field + '.whiteList[' + i + '] must not be null');
    }
    const productErr = checkProductStructure(product, field + '.whiteList[' + i + ']');
    if (productErr) {
      return productErr;
    }
  }
  if (!isSafeInteger(group.quantity)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, field + '.quantity must be a safe integer');
  }
  return null;
}

/** Content pass for a whitelist group: non-empty list, no duplicate products (the OR semantics of a group would be ambiguous), positive quantity. */
function checkItemWhiteListContent(group: ItemWhiteList, field: string): Validation {
  if (group.whiteList.length === 0) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, field + '.whiteList must be a non-empty array');
  }
  for (let i = 0; i < group.whiteList.length; i++) {
    const product = group.whiteList[i];
    const productErr = checkProductContent(product, field + '.whiteList[' + i + ']');
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

/** Structure pass for the resources: closed field set, itemsWhiteList array of valid groups. */
function checkShoppingItemsStructure(resources: ShoppingItems, field: string): Validation {
  const fieldsErr = rejectUnknownFields(resources, SHOPPING_ITEMS_FIELDS, field);
  if (fieldsErr) {
    return fieldsErr;
  }
  if (!Array.isArray(resources.itemsWhiteList)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, field + '.itemsWhiteList must be an array');
  }
  for (let i = 0; i < resources.itemsWhiteList.length; i++) {
    const groupErr = checkItemWhiteListStructure(resources.itemsWhiteList[i], field + '.itemsWhiteList[' + i + ']');
    if (groupErr) {
      return groupErr;
    }
  }
  return null;
}

/** Content pass for the resources: non-empty, valid groups, no product shared by two groups (a cart item finds its group by product alone). */
function checkShoppingItemsContent(resources: ShoppingItems, field: string): Validation {
  if (resources.itemsWhiteList.length === 0) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, field + '.itemsWhiteList must be a non-empty array');
  }
  for (let i = 0; i < resources.itemsWhiteList.length; i++) {
    const groupErr = checkItemWhiteListContent(resources.itemsWhiteList[i], field + '.itemsWhiteList[' + i + ']');
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

/** Structure pass for a cart item list: array of non-null items with valid products and safe-integer quantities. */
function checkCartItemsStructure(cartItems: CartItem[], field: string): Validation {
  if (!Array.isArray(cartItems)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, field + ' must be an array');
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
    const productErr = checkProductStructure(item.product, field + '[' + i + '].product');
    if (productErr) {
      return productErr;
    }
    if (!isSafeInteger(item.quantity)) {
      return fail(A4pErrorCode.A4P_INVALID_PARAMETER, field + '[' + i + '].quantity must be a safe integer');
    }
  }
  return null;
}

/** Content pass for a cart item list: non-empty, product values, positive quantities. */
function checkCartItemsContent(cartItems: CartItem[], field: string): Validation {
  if (cartItems.length === 0) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, field + ' must be a non-empty array');
  }
  for (let i = 0; i < cartItems.length; i++) {
    const item = cartItems[i];
    const productErr = checkProductContent(item.product, field + '[' + i + '].product');
    if (productErr) {
      return productErr;
    }
    if (!isPositiveInteger(item.quantity)) {
      return fail(A4pErrorCode.A4P_INVALID_PARAMETER, field + '[' + i + '].quantity must be a positive integer');
    }
  }
  return null;
}

/** Structure pass for a StructuredIntent: valid action enum, present resources and constraints with valid structure. */
function checkStructuredIntentStructure(si: StructuredIntent): Validation {
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
  const resourcesErr = checkShoppingItemsStructure(si.resources, 'structuredIntent.resources');
  if (resourcesErr) {
    return resourcesErr;
  }
  if (!si.constraints) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'structuredIntent.constraints must not be null');
  }
  return checkPaymentConstraintsStructure(si.constraints);
}

/** Content pass for a StructuredIntent: resource and constraint values. */
function checkStructuredIntentContent(si: StructuredIntent): Validation {
  const resourcesErr = checkShoppingItemsContent(si.resources, 'structuredIntent.resources');
  if (resourcesErr) {
    return resourcesErr;
  }
  return checkPaymentConstraintsContent(si.constraints);
}

/** Structure pass for a UserIdentity: closed field set, string identityProvider and id, string additionalInfo when provided. */
function checkUserIdentityStructure(ui: UserIdentity): Validation {
  const fieldsErr = rejectUnknownFields(ui, USER_IDENTITY_FIELDS, 'userIdentity');
  if (fieldsErr) {
    return fieldsErr;
  }
  if (!isString(ui.identityProvider)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'userIdentity.identityProvider must be a string');
  }
  if (!isString(ui.id)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'userIdentity.id must be a string');
  }
  // A non-string additionalInfo must fail loudly here rather than pass
  // validation and produce non-canonical bytes.
  if (ui.additionalInfo !== undefined && !isString(ui.additionalInfo)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'userIdentity.additionalInfo must be a string when provided');
  }
  return null;
}

/** Content pass for a UserIdentity: non-blank provider and id. */
function checkUserIdentityContent(ui: UserIdentity): Validation {
  if (!isNonBlankString(ui.identityProvider)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'userIdentity.identityProvider must not be blank');
  }
  if (!isNonBlankString(ui.id)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'userIdentity.id must not be blank');
  }
  return null;
}

/** Structure pass for an AgentIdentity: closed field set, string identityProvider and id, string additionalInfo when provided. */
function checkAgentIdentityStructure(ai: AgentIdentity): Validation {
  const fieldsErr = rejectUnknownFields(ai, AGENT_IDENTITY_FIELDS, 'agentIdentity');
  if (fieldsErr) {
    return fieldsErr;
  }
  if (!isString(ai.identityProvider)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'agentIdentity.identityProvider must be a string');
  }
  if (!isString(ai.id)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'agentIdentity.id must be a string');
  }
  if (ai.additionalInfo !== undefined && !isString(ai.additionalInfo)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'agentIdentity.additionalInfo must be a string when provided');
  }
  return null;
}

/** Content pass for an AgentIdentity: non-blank provider and id. */
function checkAgentIdentityContent(ai: AgentIdentity): Validation {
  if (!isNonBlankString(ai.identityProvider)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'agentIdentity.identityProvider must not be blank');
  }
  if (!isNonBlankString(ai.id)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'agentIdentity.id must not be blank');
  }
  return null;
}

/** Structure pass for a ServiceProviderIdentity: closed field set, string id, string additionalInfo when provided. */
function checkServiceProviderIdentityStructure(spi: ServiceProviderIdentity): Validation {
  const fieldsErr = rejectUnknownFields(spi, SERVICE_PROVIDER_FIELDS, 'serviceProviderIdentity');
  if (fieldsErr) {
    return fieldsErr;
  }
  if (!isString(spi.id)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'serviceProviderIdentity.id must be a string');
  }
  if (spi.additionalInfo !== undefined && !isString(spi.additionalInfo)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER,
      'serviceProviderIdentity.additionalInfo must be a string when provided');
  }
  return null;
}

/** Content pass for a ServiceProviderIdentity: non-blank id. */
function checkServiceProviderIdentityContent(spi: ServiceProviderIdentity): Validation {
  if (!isNonBlankString(spi.id)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'serviceProviderIdentity.id must not be blank');
  }
  return null;
}

/**
 * Validates a SigningEnv object: present device and app maps, each an
 * object (not an array or scalar). Null values inside them are tolerated:
 * serialization drops a null property and fails on a null array item, and
 * deserialization never delivers one (the strict parser rejects explicit
 * nulls at every value position).
 * Entirely structural (no content pass), so the deserialization-side
 * structure checks share it.
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
  if (!env.app || typeof env.app !== 'object' || Array.isArray(env.app)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'signingEnv.app must be an object');
  }
  return null;
}

/** Structure pass for an Order: closed field set, string orderId and currency, safe-integer amount and timestamp, cart array. */
function checkOrderStructure(order: Order): Validation {
  if (!order) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'order must not be null');
  }
  const fieldsErr = rejectUnknownFields(order, ORDER_FIELDS, 'order');
  if (fieldsErr) {
    return fieldsErr;
  }
  if (!isString(order.orderId)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'order.orderId must be a string');
  }
  if (!isSafeInteger(order.amount)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'order.amount must be a safe integer');
  }
  if (!isString(order.currency)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'order.currency must be a string');
  }
  if (!Array.isArray(order.cartItems)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'order.cartItems must be an array');
  }
  const cartErr = checkCartItemsStructure(order.cartItems, 'order.cartItems');
  if (cartErr) {
    return cartErr;
  }
  if (!isSafeInteger(order.timestamp)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'order.timestamp must be a safe integer');
  }
  return null;
}

/** Content pass for an Order: non-blank ids, positive amount, non-empty cart, non-negative timestamp. */
function checkOrderContent(order: Order): Validation {
  if (!isNonBlankString(order.orderId)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'order.orderId must not be blank');
  }
  if (!isPositiveInteger(order.amount)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'order.amount must be a positive integer');
  }
  if (!isNonBlankString(order.currency)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'order.currency must not be blank');
  }
  const cartErr = checkCartItemsContent(order.cartItems, 'order.cartItems');
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
 * Validates a finalize-API execution id. The id is a required parameter, so
 * a null or blank value is invalid input in its own right — not a lookup
 * key that merely fails to match anything — and it has no absent spelling.
 * @param executionId - The execution id to validate
 * @returns A validation failure when the id is null or blank, or null when valid
 */
export function validateExecutionId(executionId: string): Validation {
  if (!isNonBlankString(executionId)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'executionId must not be blank');
  }
  return null;
}

/** Structure pass for an Execution: valid action enum, optional string id, present well-shaped executionContent. */
function checkExecutionStructure(exec: Execution): Validation {
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
  if (exec.id !== undefined && !isString(exec.id)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'execution.id must be a string when provided');
  }
  if (!exec.executionContent) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'execution.executionContent must not be null');
  }
  return checkOrderStructure(exec.executionContent);
}

/** Content pass for an Execution: non-blank id when provided, order values. */
function checkExecutionContent(exec: Execution): Validation {
  if (exec.id !== undefined && !isNonBlankString(exec.id)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER,
      'execution.id must be a non-blank string when provided (an absent id is generated)');
  }
  return checkOrderContent(exec.executionContent);
}

/**
 * Validates an Execution object: a defined action, an optional non-blank id
 * (an absent id is generated during reservation),
 * and an executionContent that is a well-formed order.
 * @param exec - The execution to validate
 * @returns A validation failure if invalid, or null if valid
 */
export function validateExecution(exec: Execution): Validation {
  return checkExecutionStructure(exec) ?? checkExecutionContent(exec);
}

/** Structure pass for a CreateIntentRequest: closed field set, string fields, present structuredIntent. */
function checkCreateIntentRequestStructure(request: CreateIntentRequest): Validation {
  if (!request) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'request must not be null');
  }
  const fieldsErr = rejectUnknownFields(request, REQUEST_FIELDS, 'request');
  if (fieldsErr) {
    return fieldsErr;
  }
  if (!isString(request.intentConversation)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'intentConversation must be a string');
  }
  if (!isString(request.intentSummary)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'intentSummary must be a string');
  }
  if (request.intentId !== undefined && !isString(request.intentId)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'intentId must be a string when provided');
  }
  if (!request.structuredIntent) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'structuredIntent must not be null');
  }
  return checkStructuredIntentStructure(request.structuredIntent);
}

/** Content pass for a CreateIntentRequest: non-blank conversation, summary and optional id, structured-intent values. */
function checkCreateIntentRequestContent(request: CreateIntentRequest): Validation {
  if (!isNonBlankString(request.intentConversation)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'intentConversation must not be blank');
  }
  if (!isNonBlankString(request.intentSummary)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'intentSummary must not be blank');
  }
  if (request.intentId !== undefined && !isNonBlankString(request.intentId)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'intentId must not be blank when provided');
  }
  return checkStructuredIntentContent(request.structuredIntent);
}

/**
 * Validates a CreateIntentRequest object.
 * @param request - The request to validate
 * @returns A validation failure if invalid, or null if valid
 */
export function validateCreateIntentRequest(request: CreateIntentRequest): Validation {
  return checkCreateIntentRequestStructure(request) ?? checkCreateIntentRequestContent(request);
}

/** Structure pass for an IntentIssueContext: present, structurally valid identities. */
function checkIssueContextStructure(ctx: IntentIssueContext): Validation {
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
  const uiErr = checkUserIdentityStructure(ctx.userIdentity);
  if (uiErr) {
    return uiErr;
  }
  if (!ctx.agentIdentity) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'issueContext.agentIdentity must not be null');
  }
  const aiErr = checkAgentIdentityStructure(ctx.agentIdentity);
  if (aiErr) {
    return aiErr;
  }
  if (!ctx.serviceProviderIdentity) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'issueContext.serviceProviderIdentity must not be null');
  }
  const spiErr = checkServiceProviderIdentityStructure(ctx.serviceProviderIdentity);
  if (spiErr) {
    return spiErr;
  }
  return null;
}

/** Content pass for an IntentIssueContext: identity values. */
function checkIssueContextContent(ctx: IntentIssueContext): Validation {
  const uiErr = checkUserIdentityContent(ctx.userIdentity);
  if (uiErr) {
    return uiErr;
  }
  const aiErr = checkAgentIdentityContent(ctx.agentIdentity);
  if (aiErr) {
    return aiErr;
  }
  return checkServiceProviderIdentityContent(ctx.serviceProviderIdentity);
}

/**
 * Validates an IntentIssueContext object.
 * @param ctx - The issue context to validate
 * @returns A validation failure if invalid, or null if valid
 */
export function validateIssueContext(ctx: IntentIssueContext): Validation {
  return checkIssueContextStructure(ctx) ?? checkIssueContextContent(ctx);
}

/** Structure pass for signature information: valid alg enum, present string keyIdentifier and signature. */
function checkSignatureInfoStructure(info: SignatureInfo, field: string): Validation {
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
  if (!isString(info.keyIdentifier)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, field + '.sign.keyIdentifier must be a string');
  }
  if (!isString(info.sign)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, field + '.sign.sign must be a string');
  }
  return null;
}

/** Content pass for signature information: non-blank keyIdentifier and signature. */
function checkSignatureInfoContent(info: SignatureInfo, field: string): Validation {
  if (!isNonBlankString(info.keyIdentifier)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, field + '.sign.keyIdentifier must not be blank');
  }
  if (!isNonBlankString(info.sign)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, field + '.sign.sign must not be blank');
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
  return checkSignatureInfoStructure(info, field) ?? checkSignatureInfoContent(info, field);
}

/**
 * Structure pass for an IntentL1 — the layer deserialization runs (see
 * the file header).
 * @param intent - The Level 1 intent to check
 * @returns A validation failure if structurally invalid, or null if valid
 */
export function checkIntentL1Structure(intent: IntentL1): Validation {
  if (!intent) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'intentL1 must not be null');
  }
  const fieldsErr = rejectUnknownFields(intent, L1_FIELDS, 'intentL1');
  if (fieldsErr) {
    return fieldsErr;
  }
  if (!isString(intent.version)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'intentL1.version must be a string');
  }
  if (!isString(intent.intentId)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'intentL1.intentId must be a string');
  }
  if (!isString(intent.intentConversation)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'intentL1.intentConversation must be a string');
  }
  if (!isString(intent.intentSummary)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'intentL1.intentSummary must be a string');
  }
  if (!intent.structuredIntent) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'intentL1.structuredIntent must not be null');
  }
  const siErr = checkStructuredIntentStructure(intent.structuredIntent);
  if (siErr) {
    return siErr;
  }
  if (!isSafeInteger(intent.timestamp)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'intentL1.timestamp must be a safe integer');
  }
  return checkSignatureInfoStructure(intent.sign, 'intentL1');
}

/** Content pass for an IntentL1: non-blank fields, structured-intent values, non-negative timestamp, non-blank signature values. */
function checkIntentL1Content(intent: IntentL1): Validation {
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
  const siErr = checkStructuredIntentContent(intent.structuredIntent);
  if (siErr) {
    return siErr;
  }
  if (!isNonNegativeInteger(intent.timestamp)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'intentL1.timestamp must be a non-negative integer');
  }
  return checkSignatureInfoContent(intent.sign as SignatureInfo, 'intentL1');
}

/**
 * Validates an IntentL1 object structurally. The `timestamp` field must be
 * a non-negative integer; no cross-level ordering is checked here.
 * @param intent - The Level 1 intent to validate
 * @returns A validation failure if invalid, or null if valid
 */
export function validateIntentL1(intent: IntentL1): Validation {
  return checkIntentL1Structure(intent) ?? checkIntentL1Content(intent);
}

/**
 * Structure pass for an IntentL2 — the layer deserialization runs (see
 * the file header).
 * @param intent - The Level 2 intent to check
 * @returns A validation failure if structurally invalid, or null if valid
 */
export function checkIntentL2Structure(intent: IntentL2): Validation {
  if (!intent) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'intentL2 must not be null');
  }
  const fieldsErr = rejectUnknownFields(intent, L2_FIELDS, 'intentL2');
  if (fieldsErr) {
    return fieldsErr;
  }
  if (!isString(intent.version)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'intentL2.version must be a string');
  }
  if (!intent.intentL1) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'intentL2.intentL1 must not be null');
  }
  const l1Err = checkIntentL1Structure(intent.intentL1);
  if (l1Err) {
    return l1Err;
  }
  if (!intent.userIdentity) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'intentL2.userIdentity must not be null');
  }
  const uiErr = checkUserIdentityStructure(intent.userIdentity);
  if (uiErr) {
    return uiErr;
  }
  if (!intent.agentIdentity) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'intentL2.agentIdentity must not be null');
  }
  const aiErr = checkAgentIdentityStructure(intent.agentIdentity);
  if (aiErr) {
    return aiErr;
  }
  if (!intent.serviceProviderIdentity) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'intentL2.serviceProviderIdentity must not be null');
  }
  const spiErr = checkServiceProviderIdentityStructure(intent.serviceProviderIdentity);
  if (spiErr) {
    return spiErr;
  }
  if (!isSafeInteger(intent.timestamp)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'intentL2.timestamp must be a safe integer');
  }
  return checkSignatureInfoStructure(intent.sign, 'intentL2');
}

/** Content pass for an IntentL2: non-blank version, L1 values, identity values, non-negative timestamp, non-blank signature values. */
function checkIntentL2Content(intent: IntentL2): Validation {
  if (!isNonBlankString(intent.version)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'intentL2.version must not be blank');
  }
  const l1Err = checkIntentL1Content(intent.intentL1);
  if (l1Err) {
    return l1Err;
  }
  const uiErr = checkUserIdentityContent(intent.userIdentity);
  if (uiErr) {
    return uiErr;
  }
  const aiErr = checkAgentIdentityContent(intent.agentIdentity);
  if (aiErr) {
    return aiErr;
  }
  const spiErr = checkServiceProviderIdentityContent(intent.serviceProviderIdentity);
  if (spiErr) {
    return spiErr;
  }
  if (!isNonNegativeInteger(intent.timestamp)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'intentL2.timestamp must be a non-negative integer');
  }
  return checkSignatureInfoContent(intent.sign as SignatureInfo, 'intentL2');
}

/**
 * Validates an IntentL2 object structurally, including its embedded L1.
 * The `timestamp` fields must be non-negative integers; no cross-level
 * ordering is checked here.
 * @param intent - The Level 2 intent to validate
 * @returns A validation failure if invalid, or null if valid
 */
export function validateIntentL2(intent: IntentL2): Validation {
  return checkIntentL2Structure(intent) ?? checkIntentL2Content(intent);
}

/**
 * Structure pass for an IntentL3 — the layer deserialization runs (see
 * the file header).
 * @param intent - The Level 3 intent to check
 * @returns A validation failure if structurally invalid, or null if valid
 */
export function checkIntentL3Structure(intent: IntentL3): Validation {
  if (!intent) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'intentL3 must not be null');
  }
  const fieldsErr = rejectUnknownFields(intent, L3_FIELDS, 'intentL3');
  if (fieldsErr) {
    return fieldsErr;
  }
  if (!isString(intent.version)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'intentL3.version must be a string');
  }
  if (!intent.intentL2) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'intentL3.intentL2 must not be null');
  }
  const l2Err = checkIntentL2Structure(intent.intentL2);
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
  if (!isSafeInteger(intent.timestamp)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'intentL3.timestamp must be a safe integer');
  }
  return checkSignatureInfoStructure(intent.sign, 'intentL3');
}

/** Content pass for an IntentL3: non-blank version, L2/L1 values, non-negative timestamp, non-blank signature values. */
function checkIntentL3Content(intent: IntentL3): Validation {
  if (!isNonBlankString(intent.version)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'intentL3.version must not be blank');
  }
  const l2Err = checkIntentL2Content(intent.intentL2);
  if (l2Err) {
    return l2Err;
  }
  if (!isNonNegativeInteger(intent.timestamp)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'intentL3.timestamp must be a non-negative integer');
  }
  return checkSignatureInfoContent(intent.sign as SignatureInfo, 'intentL3');
}

/**
 * Validates an IntentL3 object structurally, including its embedded L2 and L1.
 * The `timestamp` fields must be non-negative integers; no cross-level
 * ordering is checked here.
 * @param intent - The Level 3 intent to validate
 * @returns A validation failure if invalid, or null if valid
 */
export function validateIntentL3(intent: IntentL3): Validation {
  return checkIntentL3Structure(intent) ?? checkIntentL3Content(intent);
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
 * Structure pass for an execution record: defined status and action, safe-integer stamps, status-bound finalization
 * presence (COMPLETED carries completedAt, CANCELED canceledAt — the converse is content), well-shaped order.
 */
function checkExecutionRecordStructure(record: ExecutionRecord, field: string): Validation {
  if (!record) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, field + ' must not be null');
  }
  const fieldsErr = rejectUnknownFields(record, RECORD_FIELDS, field);
  if (fieldsErr) {
    return fieldsErr;
  }
  if (!isString(record.executionId)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, field + '.executionId must be a string');
  }
  if (!isValidExecutionStatus(record.status)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, field + '.status must be a valid ExecutionStatus');
  }
  // Whether the index must be present at all is a periodicity matter,
  // judged by the context content pass; a present one is a safe integer.
  if (record.periodIndex !== undefined && !isSafeInteger(record.periodIndex)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER,
      field + '.periodIndex must be a safe integer when present');
  }
  if (!isSafeInteger(record.reservedAt)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, field + '.reservedAt must be a safe integer');
  }
  if (record.completedAt !== undefined && !isSafeInteger(record.completedAt)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER,
      field + '.completedAt must be a safe integer when present');
  }
  if (record.canceledAt !== undefined && !isSafeInteger(record.canceledAt)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER,
      field + '.canceledAt must be a safe integer when present');
  }
  if (record.status === ExecutionStatus.COMPLETED && record.completedAt === undefined) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER,
      field + '.completedAt must be present on a COMPLETED record');
  }
  if (record.status === ExecutionStatus.CANCELED && record.canceledAt === undefined) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER,
      field + '.canceledAt must be present on a CANCELED record');
  }
  if (!isValidIntentActionType(record.action)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, field + '.action must be a valid IntentActionType');
  }
  if (!record.executionContent) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, field + '.executionContent must not be null');
  }
  return checkOrderStructure(record.executionContent);
}

/**
 * Content pass for an execution record: non-blank executionId, non-negative stamps, converse status binding,
 * monotonic finalization (A4P_TIMESTAMP_TOO_EARLY — the code the offending
 * operation would have returned), order values.
 */
function checkExecutionRecordContent(record: ExecutionRecord, field: string): Validation {
  if (!isNonBlankString(record.executionId)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, field + '.executionId must not be blank');
  }
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
  // The converse of the structure pass's status binding.
  if (record.status !== ExecutionStatus.COMPLETED && record.completedAt !== undefined) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER,
      field + '.completedAt must be present only on a COMPLETED record');
  }
  if (record.status !== ExecutionStatus.CANCELED && record.canceledAt !== undefined) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER,
      field + '.canceledAt must be present only on a CANCELED record');
  }
  // A finalization can never predate the reservation it finalizes
  // (completeExecution/cancelExecution reject such a clock), so a record
  // violating it is corrupted data.
  if (record.completedAt !== undefined && record.completedAt < record.reservedAt) {
    return fail(A4pErrorCode.A4P_TIMESTAMP_TOO_EARLY,
      field + '.completedAt must not be earlier than ' + field + '.reservedAt');
  }
  if (record.canceledAt !== undefined && record.canceledAt < record.reservedAt) {
    return fail(A4pErrorCode.A4P_TIMESTAMP_TOO_EARLY,
      field + '.canceledAt must not be earlier than ' + field + '.reservedAt');
  }
  return checkOrderContent(record.executionContent);
}

/**
 * Structure pass for a payment execution info snapshot: closed field set, present safe-integer leftAmount,
 * safe-integer optional figures. (The snapshot period index may be negative: an activation before the anchor
 * snapshots the pre-anchor period — activateIntent judges only validUntil.)
 */
function checkExecutionInfoStructure(info: PaymentExecutionInfo, field: string): Validation {
  if (!info) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, field + ' must not be null');
  }
  const fieldsErr = rejectUnknownFields(info, EXECUTION_INFO_FIELDS, field);
  if (fieldsErr) {
    return fieldsErr;
  }
  if (info.periodIndex !== undefined && !isSafeInteger(info.periodIndex)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER,
      field + '.periodIndex must be a safe integer when present');
  }
  if (info.leftPeriodExecutionCount !== undefined && !isSafeInteger(info.leftPeriodExecutionCount)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER,
      field + '.leftPeriodExecutionCount must be a safe integer when present');
  }
  if (info.leftExecutionCount !== undefined && !isSafeInteger(info.leftExecutionCount)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER,
      field + '.leftExecutionCount must be a safe integer when present');
  }
  if (!isSafeInteger(info.leftAmount)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, field + '.leftAmount must be a safe integer');
  }
  return null;
}

/** Content pass for a payment execution info snapshot: non-negative figures. */
function checkExecutionInfoContent(info: PaymentExecutionInfo, field: string): Validation {
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
 * Structure pass for an IntentExecutionContext — the layer
 * deserialization runs (see the file header): string version and
 * intentId, the full authorized chain, a defined status, safe-integer
 * stamps, an executions array of valid records, a well-shaped snapshot.
 * @param context - The execution context to check
 * @returns A validation failure if structurally invalid, or null if valid
 */
export function checkIntentExecutionContextStructure(context: IntentExecutionContext): Validation {
  if (!context) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'context must not be null');
  }
  const fieldsErr = rejectUnknownFields(context, CONTEXT_FIELDS, 'context');
  if (fieldsErr) {
    return fieldsErr;
  }
  if (!isString(context.version)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'context.version must be a string');
  }
  if (!isString(context.intentId)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'context.intentId must be a string');
  }
  if (!context.authorizedIntent) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'context.authorizedIntent must not be null');
  }
  const intentErr = checkIntentL3Structure(context.authorizedIntent);
  if (intentErr) {
    return intentErr;
  }
  if (!isValidIntentExecutionStatus(context.status)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'context.status must be a valid IntentExecutionStatus');
  }
  if (!isSafeInteger(context.activatedTime)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'context.activatedTime must be a safe integer');
  }
  if (context.revokedTime !== undefined && !isSafeInteger(context.revokedTime)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER,
      'context.revokedTime must be a safe integer when present');
  }
  // The ledger may be empty (nothing reserved yet) but must be an array.
  if (!Array.isArray(context.executions)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'context.executions must be an array');
  }
  for (let i = 0; i < context.executions.length; i++) {
    const recordErr = checkExecutionRecordStructure(context.executions[i], 'context.executions[' + i + ']');
    if (recordErr) {
      return recordErr;
    }
  }
  if (!context.executionInfo) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'context.executionInfo must not be null');
  }
  return checkExecutionInfoStructure(context.executionInfo, 'context.executionInfo');
}

/**
 * Content pass for an IntentExecutionContext: non-blank version and
 * intentId, the authorized chain's content values (checkIntentL3Content —
 * the structure pass judged only its shape, and the codec restored its
 * values verbatim; the execution APIs are where they are judged), the
 * intentId derivation, non-negative stamps, record values,
 * snapshot figures, and the cap-tied presence rules (records carry a
 * periodIndex exactly when periodic; the snapshot carries its period
 * figures exactly when periodic and leftExecutionCount exactly when the
 * intent has a maxExecutionCount cap).
 */
function checkIntentExecutionContextContent(context: IntentExecutionContext): Validation {
  if (!isNonBlankString(context.version)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'context.version must not be blank');
  }
  if (!isNonBlankString(context.intentId)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'context.intentId must not be blank');
  }
  const chainErr = checkIntentL3Content(context.authorizedIntent);
  if (chainErr) {
    return chainErr;
  }
  const l1: IntentL1 = context.authorizedIntent.intentL2.intentL1;
  // intentId is derived from the authorized chain at activation; a
  // mismatched id is corrupted data, not an alternative spelling.
  if (context.intentId !== l1.intentId) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER,
      'context.intentId must equal intentL1.intentId of the authorized chain');
  }
  // Neither stamp is compared with any other timestamp, and revokedTime
  // is not bound to the status.
  if (!isNonNegativeInteger(context.activatedTime)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER, 'context.activatedTime must be a non-negative integer');
  }
  if (context.revokedTime !== undefined && !isNonNegativeInteger(context.revokedTime)) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER,
      'context.revokedTime must be a non-negative integer when present');
  }
  // The period rule is all-or-nothing (validated with the intent chain),
  // so the anchor alone decides periodicity.
  const periodic: boolean = l1.structuredIntent.constraints.anchorTime !== undefined;
  for (let i = 0; i < context.executions.length; i++) {
    const recordErr = checkExecutionRecordContent(context.executions[i], 'context.executions[' + i + ']');
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
  const infoErr = checkExecutionInfoContent(context.executionInfo, 'context.executionInfo');
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
  // A null never reaches here — the structure pass (and, for the intent
  // chain, checkPaymentConstraintsStructure) rejects it; undefined is
  // the only absent spelling.
  const countCap: number | undefined = l1.structuredIntent.constraints.maxExecutionCount;
  if (countCap !== undefined && context.executionInfo.leftExecutionCount === undefined) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER,
      'context.executionInfo.leftExecutionCount must be present when the intent carries maxExecutionCount');
  }
  if (countCap === undefined && context.executionInfo.leftExecutionCount !== undefined) {
    return fail(A4pErrorCode.A4P_INVALID_PARAMETER,
      'context.executionInfo.leftExecutionCount must be absent when the intent carries no maxExecutionCount');
  }
  return null;
}

/**
 * Validates an IntentExecutionContext object structurally: the full
 * authorized intent chain (both the structure and the content layer),
 * the intentId derivation,
 * a defined lifecycle status, the activation and revocation stamps
 * (activatedTime must be a non-negative integer, revokedTime too when
 * present; no ordering or status-binding check applies to them), every
 * ledger record, and the accounting
 * snapshot — including the cap-tied presence rules: records carry a
 * periodIndex exactly when the intent is periodic, and the snapshot
 * carries its period figures exactly when periodic and
 * leftExecutionCount exactly when the intent has a maxExecutionCount cap.
 * Used by the execution APIs
 * (revoke/create/complete/cancel): the caller's context is untrusted
 * in-memory state whose fields the APIs dereference directly, so a
 * malformed one must surface as a validation failure — an A4pError —
 * instead of a raw TypeError.
 * @param context - The execution context to validate
 * @returns A validation failure if invalid, or null if valid
 */
export function validateIntentExecutionContext(context: IntentExecutionContext): Validation {
  return checkIntentExecutionContextStructure(context) ?? checkIntentExecutionContextContent(context);
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
 * Checks that an execution context and its authorized intent chain carry
 * the protocol version: the context's own `version` field first, then the
 * full chain (see checkChainVersions). Shared by the execution-class APIs
 * (revokeIntent, createExecution, completeExecution, cancelExecution).
 * @param context - The execution context to check
 * @returns A validation failure if any version mismatches, or null if all match
 */
export function checkExecutionContextVersions(context: IntentExecutionContext): A4pValidationFailure | null {
  if (context.version != PROTOCOL_VERSION) {
    return new A4pValidationFailure(A4pErrorCode.A4P_UNSUPPORTED_VERSION,
      'Context version ' + context.version + ' is not supported');
  }
  return checkChainVersions(context.authorizedIntent);
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
