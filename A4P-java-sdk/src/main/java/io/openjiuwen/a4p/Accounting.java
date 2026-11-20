package io.openjiuwen.a4p;

import io.openjiuwen.a4p.Models.*;
import java.math.BigInteger;
import java.util.Objects;

/**
 * Computes payment quota usage from the execution ledger.
 * PENDING and COMPLETED records occupy quota; CANCELED records release it.
 * Callers synchronize access to mutable contexts when computing or updating accounting.
 */
final class Accounting {
    /**
     * Prevents instantiation of this utility class.
     */
    private Accounting() {}

    /**
     * Computes floor((now - anchorTime) / (periodSeconds * 1000)), or null without periodic limits.
     * Uses arbitrary-precision intermediates to avoid overflow.
     * @param constraints authorized AI_PAY payment limits
     * @param now trusted non-negative Unix epoch milliseconds within the safe integer range
     * @return computed or stored accounting period, or null without periodic limits
     */
    static Long period(PaymentConstraints constraints, long now) {
        if (constraints.anchorTime() == null) { return null; }
        BigInteger elapsed = BigInteger.valueOf(now).subtract(BigInteger.valueOf(constraints.anchorTime()));
        BigInteger duration = BigInteger.valueOf(constraints.periodSeconds()).multiply(BigInteger.valueOf(1000));
        BigInteger[] quotient = elapsed.divideAndRemainder(duration);
        return (elapsed.signum() < 0 && quotient[1].signum() != 0
                ? quotient[0].subtract(BigInteger.ONE) : quotient[0]).longValueExact();
    }

    /**
     * Reports whether a ledger entry occupies quota; canceled entries do not.
     * @param entry ledger entry to evaluate
     * @return true if the record occupies quota
     */
    static boolean counted(IntentExecutionContext.Entry entry) {
        return entry.record().status() != ExecutionStatus.CANCELED;
    }

    /**
     * Counts pending and completed executions across all periods.
     * @param context activated intent and mutable execution ledger
     * @return number of pending and completed executions
     */
    static long count(IntentExecutionContext context) {
        return context.ledger().stream().filter(Accounting::counted).count();
    }

    /**
     * Sums amounts reserved by pending and completed executions.
     * @param context activated intent and mutable execution ledger
     * @return total amount occupied by pending and completed records
     */
    static long amount(IntentExecutionContext context) {
        return context.ledger().stream().filter(Accounting::counted).mapToLong(entry -> entry.order().amount()).sum();
    }

    /**
     * Subtracts occupied amounts from the authorized total amount.
     * @param context activated intent and mutable execution ledger
     * @return remaining global amount
     */
    static long leftAmount(IntentExecutionContext context) {
        return context.constraints().totalMaxAmount() - amount(context);
    }

    // The global quota is permanently used up and nothing is pending, so an ACTIVE intent becomes CONSUMED.
    /**
     * Tests whether global amount or count quota is exhausted with no pending executions.
     * @param context activated intent and mutable execution ledger
     * @return true if global quota is exhausted and no execution is pending
     */
    static boolean exhausted(IntentExecutionContext context) {
        boolean pending = context.ledger().stream()
                .anyMatch(entry -> entry.record().status() == ExecutionStatus.PENDING);
        Long countCap = context.constraints().maxExecutionCount();
        boolean countUsedUp = countCap != null && count(context) >= countCap;
        return !pending && (countUsedUp || leftAmount(context) == 0);
    }

    /**
     * Counts quota-occupying reservations assigned to the specified original period.
     * @param context activated intent and mutable execution ledger
     * @param period accounting period index, or null when periodic limits are absent
     * @return occupied execution count in the specified period
     */
    static long periodCount(IntentExecutionContext context, Long period) {
        return context.ledger().stream().filter(Accounting::counted)
                .filter(entry -> Objects.equals(entry.record().periodIndex(), period)).count();
    }

    /**
     * Refreshes the quota snapshot for the trusted evaluation instant without moving reservations between periods.
     * @param context activated intent and mutable execution ledger
     * @param now trusted non-negative Unix epoch milliseconds within the safe integer range
     */
    static void recompute(IntentExecutionContext context, long now) {
        context.executionInfo(executionInfo(context, period(context.constraints(), now)));
    }

    /**
     * Computes remaining global and per-period quotas; unconfigured optional quotas are null.
     * @param context activated intent and mutable execution ledger
     * @param period accounting period index, or null when periodic limits are absent
     * @return quota snapshot without advancing its period
     */
    static PaymentExecutionInfo executionInfo(IntentExecutionContext context, Long period) {
        PaymentConstraints constraints = context.constraints();
        return new PaymentExecutionInfo(
                constraints.maxExecutionCount() == null ? null : constraints.maxExecutionCount() - count(context),
                leftAmount(context),
                period,
                period == null ? null : constraints.maxExecutionCountPerPeriod() - periodCount(context, period));
    }
}
