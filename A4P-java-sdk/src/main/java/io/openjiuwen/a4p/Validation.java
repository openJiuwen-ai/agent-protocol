package io.openjiuwen.a4p;

import io.openjiuwen.a4p.Models.*;
import java.math.BigInteger;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Set;
import static io.openjiuwen.a4p.A4pErrorCode.*;

final class Validation {
    static final long MAX_PERIOD_SECONDS = CanonicalJson.MAX_SAFE_INTEGER / 1000;

    private Validation() {}

    static void require(boolean condition, A4pErrorCode code) {
        if (!condition) { throw new A4pException(code); }
    }

    static void present(Object value) { require(value != null, A4P_INVALID_PARAMETER); }
    static void text(String value) { require(value != null && !blank(value), A4P_INVALID_PARAMETER); }
    static void optionalText(String value) { if (value != null) { text(value); } }

    // Matches ArkTS String.prototype.trim(), whose whitespace set differs from Character.isWhitespace.
    private static boolean blank(String value) {
        return value.chars().allMatch(character -> character == '\t' || character == 0x0B || character == '\f'
                || character == ' ' || character == 0xA0 || character == 0xFEFF || character == '\n'
                || character == '\r' || character == 0x2028 || character == 0x2029
                || Character.getType(character) == Character.SPACE_SEPARATOR);
    }

    static void integer(Long value, boolean positive) {
        require(value != null && value >= (positive ? 1 : 0)
                && value <= CanonicalJson.MAX_SAFE_INTEGER, A4P_INVALID_PARAMETER);
    }

    static void request(CreateIntentRequest request) {
        present(request);
        text(request.intentConversation());
        text(request.intentSummary());
        optionalText(request.intentId());
        structured(request.structuredIntent());
    }

    private static void structured(StructuredIntent intent) {
        present(intent);
        require(intent.action() == IntentActionType.AI_PAY, A4P_INVALID_PARAMETER);
        ShoppingItems resources = PaymentProfile.resources(intent);
        nonempty(resources.itemsWhiteList());
        for (ItemWhiteList group : resources.itemsWhiteList()) {
            present(group);
            integer(group.quantity(), true);
            nonempty(group.whiteList());
            group.whiteList().forEach(Validation::product);
        }
        PaymentConstraints constraints = PaymentProfile.constraints(intent);
        integer(constraints.totalMaxAmount(), true);
        text(constraints.currency());
        integer(constraints.validFrom(), false);
        integer(constraints.validUntil(), false);
        require(constraints.validFrom() <= constraints.validUntil(), A4P_INVALID_PARAMETER);
        if (constraints.singleOrderMaxAmount() != null) { integer(constraints.singleOrderMaxAmount(), true); }
        if (constraints.maxExecuteCount() != null) { integer(constraints.maxExecuteCount(), true); }
        if (constraints.anchorTime() != null || constraints.periodSeconds() != null
                || constraints.maxExecutionCountPerPeriod() != null) {
            integer(constraints.anchorTime(), false);
            // A later anchor would give pre-anchor reservations negative periods, each with a fresh budget.
            require(constraints.anchorTime() <= constraints.validFrom(), A4P_INVALID_PARAMETER);
            integer(constraints.periodSeconds(), true);
            require(constraints.periodSeconds() <= MAX_PERIOD_SECONDS, A4P_INVALID_PARAMETER);
            integer(constraints.maxExecutionCountPerPeriod(), true);
        }
    }

    // Seller id + spuId + skuId is the whitelist matching key, so blank ids must not match vacuously.
    private static void product(Product product) {
        present(product);
        present(product.seller());
        text(product.seller().id());
        present(product.spusku());
        optionalText(product.spusku().spuId());
        optionalText(product.spusku().skuId());
        require(product.spusku().spuId() != null || product.spusku().skuId() != null, A4P_INVALID_PARAMETER);
    }

    private static void nonempty(List<?> values) {
        require(values != null && !values.isEmpty(), A4P_INVALID_PARAMETER);
    }

    static void issueContext(IntentIssueContext context) {
        present(context);
        present(context.userIdentity());
        text(context.userIdentity().identityProvider());
        text(context.userIdentity().id());
        present(context.agentIdentity());
        text(context.agentIdentity().identityProvider());
        text(context.agentIdentity().id());
        present(context.serviceProviderIdentity());
        text(context.serviceProviderIdentity().id());
    }

    static void signingEnv(SigningEnv environment) {
        present(environment);
        present(environment.device());
        present(environment.app());
    }

    // alg and keyIdentifier are detached from the signed bytes, so unknown algorithms are rejected structurally.
    static void signature(SignatureInfo signature) {
        present(signature);
        require(AlgorithmType.RSA3072.name().equals(signature.alg()), A4P_INVALID_PARAMETER);
        text(signature.keyIdentifier());
        text(signature.sign());
    }

    static void intentL1(IntentL1 intent) {
        present(intent);
        text(intent.version());
        text(intent.intentId());
        request(new CreateIntentRequest(intent.intentConversation(), intent.intentSummary(),
                intent.structuredIntent(), intent.intentId()));
        integer(intent.timestamp(), false);
        signature(intent.sign());
    }

    static void intentL2(IntentL2 intent) {
        present(intent);
        text(intent.version());
        intentL1(intent.intentL1());
        issueContext(new IntentIssueContext(intent.userIdentity(), intent.agentIdentity(),
                intent.serviceProviderIdentity()));
        integer(intent.timestamp(), false);
        require(intent.timestamp() >= intent.intentL1().timestamp(), A4P_INVALID_PARAMETER);
        signature(intent.sign());
    }

    static void intentL3(IntentL3 intent) {
        present(intent);
        text(intent.version());
        intentL2(intent.intentL2());
        signingEnv(intent.signingEnv());
        integer(intent.timestamp(), false);
        require(intent.timestamp() >= intent.intentL2().timestamp(), A4P_INVALID_PARAMETER);
        signature(intent.sign());
    }

    // Returns the AI_PAY order parsed from the typed-union executionContent.
    static Order execution(Execution execution) {
        present(execution);
        require(execution.action() == IntentActionType.AI_PAY, A4P_INVALID_PARAMETER);
        text(execution.version());
        optionalText(execution.id());
        Order order = PaymentProfile.order(execution);
        text(order.orderId());
        integer(order.amount(), true);
        text(order.currency());
        integer(order.timestamp(), false);
        nonempty(order.cartItems());
        for (CartItem item : order.cartItems()) {
            present(item);
            product(item.product());
            integer(item.quantity(), true);
        }
        return order;
    }

    static void versions(IntentL3 intent) {
        version(intent.version());
        version(intent.intentL2().version());
        version(intent.intentL2().intentL1().version());
    }

    // A persisted ledger is untrusted: it must be one the execution operations could have produced for the
    // authorized intent, and a violation fails with the code the offending operation would have returned.
    static void ledger(IntentExecutionContext context) {
        IntentL1 intent = context.authorizedIntent().intentL2().intentL1();
        PaymentConstraints constraints = context.constraints();
        List<ItemWhiteList> groups = context.resources().itemsWhiteList();
        Set<String> executionIds = new HashSet<>();
        Set<String> orderIds = new HashSet<>();
        Map<Long, Long> periodCounts = new HashMap<>();
        BigInteger amount = BigInteger.ZERO;
        for (IntentExecutionContext.Entry entry : context.ledger()) {
            ExecutionRecord record = entry.record();
            Order order = entry.order();
            text(record.executionId());
            present(record.status());
            require(record.executionId().equals(record.execution().id()), A4P_INVALID_PARAMETER);
            require(record.execution().version().equals(intent.version()), A4P_UNSUPPORTED_VERSION);
            require(record.execution().action() == intent.structuredIntent().action(), A4P_INVALID_PARAMETER);
            require(order.currency().equals(constraints.currency()), A4P_INVALID_PARAMETER);
            require(record.reservedAt() >= constraints.validFrom(), A4P_TIMESTAMP_TOO_EARLY);
            require(record.reservedAt() <= constraints.validUntil(), A4P_TIMESTAMP_EXPIRED);
            require(Objects.equals(record.periodIndex(), Accounting.period(constraints, record.reservedAt())),
                    A4P_INVALID_PARAMETER);
            finalizedAt(record.completedAt(), record.status() == ExecutionStatus.COMPLETED, record.reservedAt());
            finalizedAt(record.canceledAt(), record.status() == ExecutionStatus.CANCELED, record.reservedAt());
            require(executionIds.add(record.executionId()), A4P_DUPLICATE_ID);
            require(orderIds.add(order.orderId()), A4P_DUPLICATE_ID);
            if (constraints.singleOrderMaxAmount() != null) {
                require(order.amount() <= constraints.singleOrderMaxAmount(), A4P_SINGLE_ORDER_AMOUNT_EXCEEDED);
            }
            CartMatching.requireOneToOne(order.cartItems(), groups);
            if (Accounting.counted(entry)) {
                amount = amount.add(BigInteger.valueOf(order.amount()));
                if (record.periodIndex() != null) { periodCounts.merge(record.periodIndex(), 1L, Long::sum); }
            }
        }
        if (constraints.maxExecuteCount() != null) {
            require(Accounting.count(context) <= constraints.maxExecuteCount(), A4P_EXECUTION_COUNT_EXCEEDED);
        }
        require(amount.compareTo(BigInteger.valueOf(constraints.totalMaxAmount())) <= 0, A4P_AMOUNT_EXCEEDED);
        if (constraints.maxExecutionCountPerPeriod() != null) {
            require(periodCounts.values().stream().allMatch(count -> count <= constraints.maxExecutionCountPerPeriod()),
                    A4P_PERIOD_EXECUTION_COUNT_EXCEEDED);
        }
        for (ItemWhiteList group : groups) {
            BigInteger accumulated = context.ledger().stream().filter(Accounting::counted)
                    .map(entry -> CartMatching.quantity(entry.order().cartItems(), group))
                    .reduce(BigInteger.ZERO, BigInteger::add);
            require(accumulated.compareTo(BigInteger.valueOf(group.quantity())) <= 0, A4P_PRODUCT_QUANTITY_EXCEEDED);
        }
        // Completing the last pending record of an exhausted ACTIVE intent always yields CONSUMED, and nothing
        // else does; REVOKED and EXPIRED are reachable with any ledger.
        IntentExecutionStatus status = context.status();
        if (status == IntentExecutionStatus.ACTIVE || status == IntentExecutionStatus.CONSUMED) {
            require((status == IntentExecutionStatus.CONSUMED) == Accounting.exhausted(context), A4P_INVALID_PARAMETER);
        }
    }

    // completedAt and canceledAt are set exactly when the record reached the matching final status, never before
    // the reservation.
    private static void finalizedAt(Long timestamp, boolean expected, long reservedAt) {
        require((timestamp != null) == expected, A4P_INVALID_PARAMETER);
        if (timestamp != null) {
            integer(timestamp, false);
            require(timestamp >= reservedAt, A4P_INVALID_PARAMETER);
        }
    }

    static void version(String version) {
        require(A4p.PROTOCOL_VERSION.equals(version), A4P_UNSUPPORTED_VERSION);
    }
}
