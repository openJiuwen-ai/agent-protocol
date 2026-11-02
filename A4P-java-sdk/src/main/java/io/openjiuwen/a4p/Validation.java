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

/**
 * Validates protocol inputs, layered intent structure and restored execution ledgers.
 * Violations throw A4pException with the appropriate protocol code; these checks do not authenticate signatures.
 */
final class Validation {
    /** Largest period whose millisecond duration is a safe integer. */
    static final long MAX_PERIOD_SECONDS = CanonicalJson.MAX_SAFE_INTEGER / 1000;

    /**
     * Prevents instantiation of this utility class.
     */
    private Validation() {}

    /**
     * Throws the supplied protocol error when a required condition is false.
     * @param condition required condition
     * @param code protocol result code; zero means success
     */
    static void require(boolean condition, A4pErrorCode code) {
        if (!condition) { throw new A4pException(code); }
    }

    /**
     * Requires a non-null protocol value.
     * @param value value to validate or normalize
     */
    static void present(Object value) { require(value != null, A4P_INVALID_PARAMETER); }
    /**
     * Requires a string that is nonblank under JavaScript trim semantics.
     * @param value value to validate or normalize
     */
    static void text(String value) { require(value != null && !blank(value), A4P_INVALID_PARAMETER); }
    /**
     * Validates a nonblank string when an optional value is present.
     * @param value value to validate or normalize
     */
    static void optionalText(String value) { if (value != null) { text(value); } }

    // Matches ArkTS String.prototype.trim(), whose whitespace set differs from Character.isWhitespace.
    /**
     * Tests whitespace using the JavaScript trim character set for cross-language compatibility.
     * @param value value to validate or normalize
     * @return true if all characters are JavaScript trim whitespace
     */
    private static boolean blank(String value) {
        return value.chars().allMatch(character -> character == '\t' || character == 0x0B || character == '\f'
                || character == ' ' || character == 0xA0 || character == 0xFEFF || character == '\n'
                || character == '\r' || character == 0x2028 || character == 0x2029
                || Character.getType(character) == Character.SPACE_SEPARATOR);
    }

    /**
     * Requires a non-negative safe integer, or a strictly positive safe integer when requested.
     * @param value value to validate or normalize
     * @param positive whether zero must also be rejected
     */
    static void integer(Long value, boolean positive) {
        require(value != null && value >= (positive ? 1 : 0)
                && value <= CanonicalJson.MAX_SAFE_INTEGER, A4P_INVALID_PARAMETER);
    }

    /**
     * Checks creation input and AI_PAY resources and constraints; does not filter conversation contents.
     * @param request creation request including the conversation, summary and structured intent
     */
    static void request(CreateIntentRequest request) {
        present(request);
        text(request.intentConversation());
        text(request.intentSummary());
        optionalText(request.intentId());
        structured(request.structuredIntent());
    }

    /**
     * Validates AI_PAY resources, disjoint whitelist groups, payment limits and complete periodic configuration.
     * @param intent protocol intent to process
     */
    private static void structured(StructuredIntent intent) {
        present(intent);
        require(intent.action() == IntentActionType.AI_PAY, A4P_INVALID_PARAMETER);
        ShoppingItems resources = PaymentProfile.resources(intent);
        nonempty(resources.itemsWhiteList());
        Set<Product> products = new HashSet<>();
        for (ItemWhiteList group : resources.itemsWhiteList()) {
            present(group);
            integer(group.quantity(), true);
            nonempty(group.whiteList());
            for (Product item : group.whiteList()) {
                product(item);
                require(products.add(item), A4P_INVALID_PARAMETER);
            }
        }
        PaymentConstraints constraints = PaymentProfile.constraints(intent);
        integer(constraints.totalMaxAmount(), true);
        text(constraints.currency());
        integer(constraints.validFrom(), false);
        integer(constraints.validUntil(), false);
        require(constraints.validFrom() <= constraints.validUntil(), A4P_INVALID_PARAMETER);
        if (constraints.singleOrderMaxAmount() != null) { integer(constraints.singleOrderMaxAmount(), true); }
        if (constraints.maxExecutionCount() != null) { integer(constraints.maxExecutionCount(), true); }
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
    /**
     * Requires a nonblank seller ID and at least one nonblank SPU or SKU identifier.
     * @param product seller and SPU/SKU product identity
     */
    private static void product(Product product) {
        present(product);
        present(product.seller());
        text(product.seller().id());
        present(product.spusku());
        optionalText(product.spusku().spuId());
        optionalText(product.spusku().skuId());
        require(product.spusku().spuId() != null || product.spusku().skuId() != null, A4P_INVALID_PARAMETER);
    }

    /**
     * Requires a non-null, nonempty list.
     * @param values list to snapshot or validate
     */
    private static void nonempty(List<?> values) {
        require(values != null && !values.isEmpty(), A4P_INVALID_PARAMETER);
    }

    /**
     * Validates required user, agent and service-provider identity fields.
     * @param context activated intent and mutable execution ledger
     */
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

    /**
     * Validates signing device and application maps and their canonical representability.
     * @param environment device and application signing environment
     */
    static void signingEnv(SigningEnv environment) {
        present(environment);
        present(environment.device());
        present(environment.app());
    }

    // alg and keyIdentifier are detached from the signed bytes, so unknown algorithms are rejected structurally.
    /**
     * Requires supported algorithm metadata and nonblank key and signature values; does not verify cryptography.
     * @param signature signature metadata to verify
     */
    static void signature(SignatureInfo signature) {
        present(signature);
        require(AlgorithmType.RSA3072.name().equals(signature.alg()), A4P_INVALID_PARAMETER);
        text(signature.keyIdentifier());
        text(signature.sign());
    }

    /**
     * Validates L1 fields, signature metadata and payment payload structure.
     * @param intent protocol intent to process
     */
    static void intentL1(IntentL1 intent) {
        present(intent);
        text(intent.version());
        text(intent.intentId());
        request(new CreateIntentRequest(intent.intentConversation(), intent.intentSummary(),
                intent.structuredIntent(), intent.intentId()));
        integer(intent.timestamp(), false);
        signature(intent.sign());
    }

    /**
     * Validates L2 and nested L1 structure, identities and nondecreasing timestamps.
     * @param intent protocol intent to process
     */
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

    /**
     * Validates L3 and nested layers, signing environment and nondecreasing timestamps.
     * @param intent protocol intent to process
     */
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
    /**
     * Validates an AI_PAY execution and its order, returning the decoded payment order.
     * @param execution action-specific execution request
     * @return validated and decoded payment order
     */
    static Order execution(Execution execution) {
        present(execution);
        require(execution.action() == IntentActionType.AI_PAY, A4P_INVALID_PARAMETER);
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

    /**
     * Requires the supported protocol version throughout the supplied intent chain.
     * @param intent protocol intent to process
     */
    static void versions(IntentL2 intent) {
        version(intent.version());
        version(intent.intentL1().version());
    }

    /**
     * Requires the supported protocol version throughout the supplied intent chain.
     * @param intent protocol intent to process
     */
    static void versions(IntentL3 intent) {
        version(intent.version());
        versions(intent.intentL2());
    }

    // A persisted ledger is untrusted: it must be one the execution operations could have produced for the
    // authorized intent, and a violation fails with the code the offending operation would have returned.
    /**
     * Validates restored record IDs, timestamps, statuses, cart templates, quotas and context-state reachability.
     * @param context activated intent and mutable execution ledger
     */
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
            require(record.action() == intent.structuredIntent().action(), A4P_INVALID_PARAMETER);
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
        if (constraints.maxExecutionCount() != null) {
            require(Accounting.count(context) <= constraints.maxExecutionCount(), A4P_EXECUTION_COUNT_EXCEEDED);
        }
        require(amount.compareTo(BigInteger.valueOf(constraints.totalMaxAmount())) <= 0, A4P_AMOUNT_EXCEEDED);
        if (constraints.maxExecutionCountPerPeriod() != null) {
            require(periodCounts.values().stream().allMatch(count -> count <= constraints.maxExecutionCountPerPeriod()),
                    A4P_PERIOD_EXECUTION_COUNT_EXCEEDED);
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
    /**
     * Requires a finalization timestamp exactly for its matching final state and never before reservation.
     * @param timestamp Unix epoch milliseconds
     * @param expected whether this finalization timestamp must be present
     * @param reservedAt reservation instant in Unix epoch milliseconds
     */
    private static void finalizedAt(Long timestamp, boolean expected, long reservedAt) {
        require((timestamp != null) == expected, A4P_INVALID_PARAMETER);
        if (timestamp != null) {
            integer(timestamp, false);
            require(timestamp >= reservedAt, A4P_TIMESTAMP_TOO_EARLY);
        }
    }

    /**
     * Requires the currently supported protocol version.
     * @param version A4P Core protocol version
     */
    static void version(String version) {
        require(A4p.PROTOCOL_VERSION.equals(version), A4P_UNSUPPORTED_VERSION);
    }
}
