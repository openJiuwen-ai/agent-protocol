/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

/**
 * Constraint Models
 * 
 * Defines constraint types for intent execution in the A4P protocol.
 */

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
