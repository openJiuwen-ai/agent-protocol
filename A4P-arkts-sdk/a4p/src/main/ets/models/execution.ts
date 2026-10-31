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
  /**
   * Unique order identifier; doubles as the payment idempotency key —
   * createExecution rejects an orderId already present in the context's
   * ledger (canceled records included)
   */
  orderId: string;
  /**
   * Payment amount in cents; must be a positive integer. Caller-declared:
   * validated only against the signed amount caps, never reconciled with
   * the cart — Product carries no price — so binding the amount to the
   * products' actual prices is the responsibility of the party holding
   * the authoritative product catalog (see createExecution)
   */
  amount: number;
  /** Currency code (e.g., "CNY", "USD") */
  currency: string;
  /** Shopping cart contents */
  cartItems: CartItem[];
  /** Order creation timestamp (milliseconds since epoch); must be a non-negative integer */
  timestamp: number;
}

/**
 * An execution requested against an activated intent: the input to
 * createExecution. The reserved ledger record stores its fields flat (see
 * ExecutionRecord): the resolved id becomes the record's `executionId`,
 * and `action`/`executionContent` are copied onto the record itself.
 */
export interface Execution {
  /** Action to perform; must match the authorized intent's action */
  action: IntentActionType;
  /** Unique execution identifier; resolved into the record's executionId (generated when absent) */
  id?: string;
  /** Execution content (order details) */
  executionContent: Order;
}

/**
 * Status of an intent's overall execution lifecycle.
 */
export enum IntentExecutionStatus {
  /** Intent is active and can accept executions */
  ACTIVE = 'ACTIVE',
  /** Intent quota is consumed; no further executions can be reserved */
  CONSUMED = 'CONSUMED',
  /** Intent has been revoked */
  REVOKED = 'REVOKED',
  /** Intent has expired */
  EXPIRED = 'EXPIRED'
}

/**
 * Status of an individual execution.
 */
export enum ExecutionStatus {
  /** Execution is pending */
  PENDING = 'PENDING',
  /** Execution is complete */
  COMPLETED = 'COMPLETED',
  /** Execution is canceled */
  CANCELED = 'CANCELED'
}

/**
 * Record of a single execution within an intent.
 */
export interface ExecutionRecord {
  /** Unique execution identifier */
  executionId: string;
  /** Current status of the execution */
  status: ExecutionStatus;
  /** Action to perform; copied from the reserved execution (matches the authorized intent's action) */
  action: IntentActionType;
  /** Execution content (order details); a deep copy of the reserved execution's content */
  executionContent: Order;
  /** Period index of the reservation; present only for periodic intents */
  periodIndex?: number;
  /** Timestamp when execution was reserved (always present) */
  reservedAt: number;
  /** Timestamp when execution was completed (only when status is COMPLETED) */
  completedAt?: number;
  /** Timestamp when execution was canceled (only when status is CANCELED) */
  canceledAt?: number;
}

/**
 * Accounting snapshot of an execution context, derived from the
 * payment constraints and the executions ledger (never maintained by
 * hand; see internal/accounting.ts). Fields whose cap does not apply are
 * omitted: `periodIndex`/`leftPeriodExecutionCount` for non-periodic
 * intents, `leftExecutionCount` when the intent carries no
 * `maxExecutionCount` cap.
 */
export interface PaymentExecutionInfo {
  /** Index of the period containing the evaluation instant; periodic intents only */
  periodIndex?: number;
  /** Executions still allowed in the current period (an explicit cancel releases a slot); periodic intents only */
  leftPeriodExecutionCount?: number;
  /**
   * Executions still allowed against the count cap (an explicit cancel
   * releases a slot); omitted when the intent carries no maxExecutionCount cap
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
   * Activation instant (milliseconds since epoch), stamped by activateIntent
   * as the reservation floor: createExecution rejects an evaluation instant
   * earlier than it — no execution may predate the context itself
   */
  activatedTime: number;
  /**
   * List of execution records (append-only ledger; canceled records stay in
   * it). The execution count is derived from the non-canceled records; it is
   * not materialized as a separate field.
   */
  executions: ExecutionRecord[];
  /** Accounting snapshot derived from the constraints and the ledger (see PaymentExecutionInfo and internal/accounting.ts) */
  executionInfo: PaymentExecutionInfo;
}
