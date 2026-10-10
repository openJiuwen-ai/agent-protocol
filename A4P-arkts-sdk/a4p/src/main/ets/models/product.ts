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
