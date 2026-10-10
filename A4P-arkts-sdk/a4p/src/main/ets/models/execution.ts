/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

/**
 * Execution Models
 * 
 * Defines execution status, records, and context types
 * for tracking intent execution state in the A4P protocol.
 */

import { IntentActionType, IntentL3 } from './intent';
import { CartItem } from './product';

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
export interface PaymentExecutionInfo {
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
  executionInfo: PaymentExecutionInfo;
}
