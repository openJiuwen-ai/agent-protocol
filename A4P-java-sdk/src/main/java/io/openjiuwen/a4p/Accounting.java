package io.openjiuwen.a4p;

import io.openjiuwen.a4p.Models.*;
import java.math.BigInteger;
import java.util.Objects;

final class Accounting {
    private Accounting() {}

    static Long period(PaymentConstraints constraints, long now) {
        if (constraints.anchorTime() == null) { return null; }
        BigInteger elapsed = BigInteger.valueOf(now).subtract(BigInteger.valueOf(constraints.anchorTime()));
        BigInteger duration = BigInteger.valueOf(constraints.periodSeconds()).multiply(BigInteger.valueOf(1000));
        BigInteger[] quotient = elapsed.divideAndRemainder(duration);
        return (elapsed.signum() < 0 && quotient[1].signum() != 0
                ? quotient[0].subtract(BigInteger.ONE) : quotient[0]).longValueExact();
    }

    static boolean counted(IntentExecutionContext.Entry entry) {
        return entry.record().status() != ExecutionStatus.CANCELED;
    }

    static long count(IntentExecutionContext context) {
        return context.ledger().stream().filter(Accounting::counted).count();
    }

    static long amount(IntentExecutionContext context) {
        return context.ledger().stream().filter(Accounting::counted).mapToLong(entry -> entry.order().amount()).sum();
    }

    static long leftAmount(IntentExecutionContext context) {
        return context.constraints().totalMaxAmount() - amount(context);
    }

    // The global quota is permanently used up and nothing is pending, so an ACTIVE intent becomes CONSUMED.
    static boolean exhausted(IntentExecutionContext context) {
        boolean pending = context.ledger().stream()
                .anyMatch(entry -> entry.record().status() == ExecutionStatus.PENDING);
        Long countCap = context.constraints().maxExecuteCount();
        boolean countUsedUp = countCap != null && count(context) >= countCap;
        return !pending && (countUsedUp || leftAmount(context) == 0);
    }

    static long periodCount(IntentExecutionContext context, Long period) {
        return context.ledger().stream().filter(Accounting::counted)
                .filter(entry -> Objects.equals(entry.record().periodIndex(), period)).count();
    }

    static void recompute(IntentExecutionContext context, long now) {
        context.executionInfo(executionInfo(context, period(context.constraints(), now)));
    }

    static PaymentExecutionInfo executionInfo(IntentExecutionContext context, Long period) {
        PaymentConstraints constraints = context.constraints();
        return new PaymentExecutionInfo(
                constraints.maxExecuteCount() == null ? null : constraints.maxExecuteCount() - count(context),
                leftAmount(context),
                period,
                period == null ? null : constraints.maxExecutionCountPerPeriod() - periodCount(context, period));
    }
}
