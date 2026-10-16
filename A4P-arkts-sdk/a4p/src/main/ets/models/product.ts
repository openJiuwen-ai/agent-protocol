/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

/**
 * Product Models
 *
 * Defines product, seller, SPU/SKU, whitelist, and cart data
 * structures used by A4P payment intents.
 */

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
