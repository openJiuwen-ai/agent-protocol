/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

import { ExecutionRecord, ExecutionStatus, PaymentExecutionInfo, IntentExecutionContext } from '../models/execution';
import { PaymentConstraints } from '../models/constraint';

/**
 * Execution accounting helpers.
 *
 * All quota figures (accumulated amount, execution count, period execution
 * count, remaining budgets) are derived from the append-only `executions`
 * ledger — never maintained as independent counters — so canceling a record
 * releases its amount, count slot, and period slot by construction. The
 * ledger itself stays append-only: `executions.length` never decreases.
 */

/**
 * The resolved period rule of a periodic intent: the all-or-nothing
 * `anchorTime`/`periodSeconds`/`maxExecutionCountPerPeriod` triple with
 * the optionality resolved away (see periodRuleOf).
 */
export interface PeriodRule {
  /** Anchor time for period calculation (milliseconds since epoch) */
  anchorTime: number;
  /** Period duration in seconds */
  periodSeconds: number;
  /** Maximum number of non-canceled executions allowed per period */
  maxExecutionCountPerPeriod: number;
}

/**
 * The period rule of the constraints, or null for non-periodic intents.
 * The rule is all-or-nothing: all three fields present (periodic) or all
 * three omitted (non-periodic); validation rejects partial rules.
 * @param constraints - The payment constraints to resolve the period rule from
 * @returns The resolved period rule, or null when the constraints are non-periodic
 */
export function periodRuleOf(constraints: PaymentConstraints): PeriodRule | null {
  const anchorTime: number | undefined = constraints.anchorTime;
  const periodSeconds: number | undefined = constraints.periodSeconds;
  const maxExecutionCountPerPeriod: number | undefined = constraints.maxExecutionCountPerPeriod;
  if (anchorTime === undefined || periodSeconds === undefined || maxExecutionCountPerPeriod === undefined) {
    return null;
  }
  return {
    anchorTime: anchorTime,
    periodSeconds: periodSeconds,
    maxExecutionCountPerPeriod: maxExecutionCountPerPeriod,
  };
}

/**
 * Period index a given time falls into, relative to the anchor.
 * @param rule - The resolved period rule defining the period structure
 * @param now - The evaluation instant in epoch milliseconds
 * @returns The zero-based period index
 */
export function computeCurrentPeriodIndex(rule: PeriodRule, now: number): number {
  return Math.floor((now - rule.anchorTime) / (rule.periodSeconds * 1000));
}

/**
 * Whether the record counts toward quotas (amount, execution count,
 * current period): canceled records are excluded (their quota was
 * released).
 */
function isCountedRecord(record: ExecutionRecord): boolean {
  return record.status !== ExecutionStatus.CANCELED;
}

/**
 * Number of non-canceled records in the ledger.
 * @param executions - The execution records ledger to count from
 * @returns The count of non-canceled records
 */
export function computeExecutionCount(executions: ExecutionRecord[]): number {
  let count: number = 0;
  for (let i = 0; i < executions.length; i++) {
    if (isCountedRecord(executions[i])) {
      count += 1;
    }
  }
  return count;
}

/**
 * Number of non-canceled records within the given period.
 * @param executions - The execution records ledger to count from
 * @param periodIndex - The period index to count records for
 * @returns The count of non-canceled records in the specified period
 */
export function computePeriodExecutionCount(executions: ExecutionRecord[], periodIndex: number): number {
  let count: number = 0;
  for (let i = 0; i < executions.length; i++) {
    if (executions[i].periodIndex === periodIndex && isCountedRecord(executions[i])) {
      count += 1;
    }
  }
  return count;
}

/**
 * Sum of `amount` across all non-canceled execution records in the ledger.
 * Canceled records are excluded because cancel is an explicit definitive
 * outcome that releases quota.
 * @param executions - The execution records ledger
 * @returns The accumulated payment amount in cents
 */
export function computeAccumulatedAmount(executions: ExecutionRecord[]): number {
  let sum: number = 0;
  for (let i = 0; i < executions.length; i++) {
    if (isCountedRecord(executions[i])) {
      sum += executions[i].execution.executionContent.amount;
    }
  }
  return sum;
}

/**
 * Derives the execution-info snapshot from the payment constraints and the
 * executions ledger:
 * - fields whose cap does not apply are omitted: `leftExecutionCount` when
 *   the intent carries no `maxExecuteCount` cap, and the period figures
 *   (`periodIndex`, `leftPeriodExecutionCount`) for non-periodic intents;
 * - all figures count only non-canceled records (an explicit cancel
 *   releases the quota);
 * - period figures are relative to the period containing `now`.
 * @param constraints - The authorized payment constraints
 * @param executions - The execution records ledger
 * @param now - The evaluation instant in epoch milliseconds
 * @returns The derived execution info
 */
export function computeExecutionInfo(constraints: PaymentConstraints, executions: ExecutionRecord[],
                                     now: number): PaymentExecutionInfo {
  const info: PaymentExecutionInfo = {
    leftAmount: constraints.totalMaxAmount - computeAccumulatedAmount(executions)
  };
  const countCap: number | undefined = constraints.maxExecuteCount;
  if (countCap !== undefined) {
    info.leftExecutionCount = countCap - computeExecutionCount(executions);
  }
  const rule: PeriodRule | null = periodRuleOf(constraints);
  if (rule !== null) {
    const periodIndex: number = computeCurrentPeriodIndex(rule, now);
    info.periodIndex = periodIndex;
    info.leftPeriodExecutionCount = rule.maxExecutionCountPerPeriod -
      computePeriodExecutionCount(executions, periodIndex);
  }
  return info;
}

/**
 * Recomputes the execution info of the context from the executions ledger.
 * Must be called after every ledger mutation so the materialized fields can
 * never drift from the records. The execution count is not materialized —
 * it is derived from the ledger's non-canceled records.
 * @param context - The execution context whose accounting fields to recompute
 * @param now - The evaluation instant in epoch milliseconds for period calculation
 */
export function recomputeAccounting(context: IntentExecutionContext, now: number): void {
  const constraints: PaymentConstraints = context.authorizedIntent.intentL2.intentL1.structuredIntent.constraints;
  context.executionInfo = computeExecutionInfo(constraints, context.executions, now);
}
