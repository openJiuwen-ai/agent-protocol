package io.openjiuwen.a4p;

import io.openjiuwen.a4p.Models.*;
import java.util.List;
import java.util.UUID;
import static io.openjiuwen.a4p.A4pErrorCode.*;
import static io.openjiuwen.a4p.Validation.require;

public final class A4p {
    public static final String PROTOCOL_VERSION = "1.0";

    private A4p() {}

    public static A4pResult<IntentL1> createIntent(CreateIntentRequest request, Signer agentSigner) {
        return createIntent(request, agentSigner, System.currentTimeMillis());
    }

    public static A4pResult<IntentL1> createIntent(CreateIntentRequest request, Signer agentSigner, long now) {
        return A4pResult.capture(() -> {
            Validation.integer(now, false);
            Validation.request(request);
            String intentId = request.intentId() == null ? UUID.randomUUID().toString() : request.intentId();
            IntentL1 unsigned = new IntentL1(PROTOCOL_VERSION, intentId, request.intentConversation(),
                    request.intentSummary(), request.structuredIntent(), now, null);
            return new IntentL1(unsigned.version(), intentId, unsigned.intentConversation(), unsigned.intentSummary(),
                    unsigned.structuredIntent(), now, sign(agentSigner, unsigned));
        });
    }

    public static A4pResult<IntentL2> issueIntent(IntentL1 intent, SignatureVerifier agentVerifier,
                                                 IntentIssueContext context, Signer serverSigner) {
        return issueIntent(intent, agentVerifier, context, serverSigner, System.currentTimeMillis());
    }

    public static A4pResult<IntentL2> issueIntent(IntentL1 intent, SignatureVerifier agentVerifier,
                                                 IntentIssueContext context, Signer serverSigner, long now) {
        return A4pResult.capture(() -> {
            Validation.integer(now, false);
            Validation.issueContext(context);
            Validation.intentL1(intent);
            Validation.version(intent.version());
            // Chain timestamps are monotonic; refuse to emit an L2 that every peer would reject.
            require(now >= intent.timestamp(), A4P_INVALID_PARAMETER);
            verifyOrThrow(agentVerifier, intent, intent.sign());
            IntentL2 unsigned = new IntentL2(PROTOCOL_VERSION, intent, context.userIdentity(),
                    context.agentIdentity(), context.serviceProviderIdentity(), now, null);
            return new IntentL2(unsigned.version(), intent, unsigned.userIdentity(), unsigned.agentIdentity(),
                    unsigned.serviceProviderIdentity(), now, sign(serverSigner, unsigned));
        });
    }

    public static A4pResult<IntentL3> signIntent(IntentL2 intent, SignatureVerifier serverVerifier,
                                                SigningEnv environment, Signer userSigner) {
        return signIntent(intent, serverVerifier, environment, userSigner, System.currentTimeMillis());
    }

    public static A4pResult<IntentL3> signIntent(IntentL2 intent, SignatureVerifier serverVerifier,
                                                SigningEnv environment, Signer userSigner, long now) {
        return A4pResult.capture(() -> {
            Validation.integer(now, false);
            Validation.signingEnv(environment);
            Validation.intentL2(intent);
            Validation.versions(intent);
            require(now >= intent.timestamp(), A4P_INVALID_PARAMETER);
            PaymentConstraints constraints = PaymentProfile.constraints(intent.intentL1().structuredIntent());
            require(now <= constraints.validUntil(), A4P_TIMESTAMP_EXPIRED);
            verifyOrThrow(serverVerifier, intent, intent.sign());
            IntentL3 unsigned = new IntentL3(PROTOCOL_VERSION, intent, environment, now, null);
            return new IntentL3(PROTOCOL_VERSION, intent, environment, now, sign(userSigner, unsigned));
        });
    }

    public static A4pResult<IntentExecutionContext> activateIntent(IntentL3 intent, SignatureVerifier userVerifier) {
        return activateIntent(intent, userVerifier, System.currentTimeMillis());
    }

    public static A4pResult<IntentExecutionContext> activateIntent(IntentL3 intent, SignatureVerifier userVerifier,
                                                                   long now) {
        return A4pResult.capture(() -> {
            Validation.integer(now, false);
            Validation.intentL3(intent);
            Validation.versions(intent);
            require(now >= intent.timestamp(), A4P_INVALID_PARAMETER);
            // An intent past validUntil could never execute; one not yet valid is activated and waits for validFrom.
            PaymentConstraints constraints = PaymentProfile.constraints(intent.intentL2().intentL1().structuredIntent());
            require(now <= constraints.validUntil(), A4P_TIMESTAMP_EXPIRED);
            verifyOrThrow(userVerifier, intent, intent.sign());
            return new IntentExecutionContext(intent, now);
        });
    }

    // Checks the whole L3 chain and the intent center's L2 signature; the user signature is not verified here.
    // It is a pure check on the intent, so it does not depend on any context status.
    public static A4pResult<VerificationResult> verifyIntentAuthenticity(IntentL3 intent,
                                                                        SignatureVerifier issuerVerifier) {
        return A4pResult.capture(() -> {
            Validation.intentL3(intent);
            Validation.versions(intent);
            return verify(issuerVerifier, intent.intentL2(), intent.intentL2().sign());
        });
    }

    public static A4pResult<IntentExecutionContext> revokeIntent(IntentExecutionContext context) {
        return revokeIntent(context, System.currentTimeMillis());
    }

    /** Revokes at the supplied epoch-millisecond instant; the validity window does not restrict revocation. */
    public static A4pResult<IntentExecutionContext> revokeIntent(IntentExecutionContext context, long now) {
        return A4pResult.capture(() -> {
            Validation.present(context);
            Validation.integer(now, false);
            synchronized (context) {
                require(context.status() == IntentExecutionStatus.ACTIVE, A4P_INVALID_STATUS);
                require(now >= context.activatedTime(), A4P_TIMESTAMP_TOO_EARLY);
                context.status(IntentExecutionStatus.REVOKED);
                return context;
            }
        });
    }

    public static A4pResult<IntentExecutionContext> createExecution(IntentExecutionContext context,
                                                                    Execution execution) {
        return createExecution(context, execution, System.currentTimeMillis());
    }

    // A failed call can still have flipped the context to EXPIRED, so callers persist the context either way.
    public static A4pResult<IntentExecutionContext> createExecution(IntentExecutionContext context,
                                                                    Execution execution, long now) {
        return A4pResult.capture(() -> reserve(context, execution, now));
    }

    public static A4pResult<IntentExecutionContext> completeExecution(IntentExecutionContext context,
                                                                      String executionId) {
        return completeExecution(context, executionId, System.currentTimeMillis());
    }

    public static A4pResult<IntentExecutionContext> completeExecution(IntentExecutionContext context,
                                                                      String executionId, long now) {
        return A4pResult.capture(() -> finalizeExecution(context, executionId, now, false));
    }

    public static A4pResult<IntentExecutionContext> cancelExecution(IntentExecutionContext context,
                                                                    String executionId) {
        return cancelExecution(context, executionId, System.currentTimeMillis());
    }

    public static A4pResult<IntentExecutionContext> cancelExecution(IntentExecutionContext context,
                                                                    String executionId, long now) {
        return A4pResult.capture(() -> finalizeExecution(context, executionId, now, true));
    }

    private static IntentExecutionContext reserve(IntentExecutionContext context, Execution execution, long now) {
        Validation.integer(now, false);
        Validation.present(context);
        Order order = Validation.execution(execution);
        String executionId = execution.id() == null ? UUID.randomUUID().toString() : execution.id();
        synchronized (context) {
            // Parameter consistency comes before state and ledger checks, and the expiry flip before duplicates.
            IntentL1 intent = context.authorizedIntent().intentL2().intentL1();
            PaymentConstraints constraints = context.constraints();
            require(execution.action() == intent.structuredIntent().action(), A4P_INVALID_PARAMETER);
            require(order.currency().equals(constraints.currency()), A4P_INVALID_PARAMETER);
            require(context.status() == IntentExecutionStatus.ACTIVE, A4P_INVALID_STATUS);
            require(now >= constraints.validFrom(), A4P_TIMESTAMP_TOO_EARLY);
            require(now >= context.activatedTime(), A4P_TIMESTAMP_TOO_EARLY);
            if (now > constraints.validUntil()) {
                context.status(IntentExecutionStatus.EXPIRED);
                throw new A4pException(A4P_TIMESTAMP_EXPIRED);
            }
            require(context.ledger().stream().noneMatch(entry -> entry.record().executionId().equals(executionId)),
                    A4P_DUPLICATE_ID);
            // orderId is the payment idempotency key, so canceled records still block reuse.
            require(context.ledger().stream().noneMatch(entry -> entry.order().orderId().equals(order.orderId())),
                    A4P_DUPLICATE_ID);
            if (constraints.maxExecutionCount() != null) {
                require(Accounting.count(context) < constraints.maxExecutionCount(), A4P_EXECUTION_COUNT_EXCEEDED);
            }
            if (constraints.singleOrderMaxAmount() != null) {
                require(order.amount() <= constraints.singleOrderMaxAmount(), A4P_SINGLE_ORDER_AMOUNT_EXCEEDED);
            }
            require(order.amount() <= Accounting.leftAmount(context), A4P_AMOUNT_EXCEEDED);
            enforceWhitelist(context, order.cartItems());
            Long period = Accounting.period(constraints, now);
            if (period != null) {
                require(Accounting.periodCount(context, period) < constraints.maxExecutionCountPerPeriod(),
                        A4P_PERIOD_EXECUTION_COUNT_EXCEEDED);
            }
            context.ledger().add(new IntentExecutionContext.Entry(new ExecutionRecord(executionId,
                    ExecutionStatus.PENDING, execution.action(), execution.executionContent(),
                    period, now, null, null), order));
            Accounting.recompute(context, now);
            return context;
        }
    }

    private static IntentExecutionContext finalizeExecution(IntentExecutionContext context, String executionId,
                                                            long now, boolean cancel) {
        Validation.integer(now, false);
        Validation.present(context);
        synchronized (context) {
            // Pending reservations made before revocation or expiry can still be finalized.
            require(context.status() == IntentExecutionStatus.ACTIVE || context.status() == IntentExecutionStatus.REVOKED
                    || context.status() == IntentExecutionStatus.EXPIRED, A4P_INVALID_STATUS);
            List<IntentExecutionContext.Entry> ledger = context.ledger();
            int index = -1;
            for (int position = 0; position < ledger.size(); position++) {
                if (ledger.get(position).record().executionId().equals(executionId)) {
                    index = position;
                    break;
                }
            }
            require(index >= 0, A4P_INVALID_PARAMETER);
            IntentExecutionContext.Entry entry = ledger.get(index);
            ExecutionRecord record = entry.record();
            require(record.status() == ExecutionStatus.PENDING, A4P_INVALID_STATUS);
            // Record timestamps are monotonic like the chain's, so a persisted ledger can be checked for them.
            require(now >= record.reservedAt(), A4P_TIMESTAMP_TOO_EARLY);
            ExecutionRecord updated = new ExecutionRecord(record.executionId(),
                    cancel ? ExecutionStatus.CANCELED : ExecutionStatus.COMPLETED,
                    record.action(), record.executionContent(), record.periodIndex(),
                    record.reservedAt(), cancel ? null : now, cancel ? now : null);
            ledger.set(index, new IntentExecutionContext.Entry(updated, entry.order()));
            Accounting.recompute(context, now);
            if (!cancel && context.status() == IntentExecutionStatus.ACTIVE && Accounting.exhausted(context)) {
                // REVOKED and EXPIRED are terminal; draining their ledger does not rewrite them.
                context.status(IntentExecutionStatus.CONSUMED);
            }
            return context;
        }
    }

    private static void enforceWhitelist(IntentExecutionContext context, List<CartItem> items) {
        List<ItemWhiteList> groups = context.resources().itemsWhiteList();
        CartMatching.requireOneToOne(items, groups);
    }

    private static SignatureInfo sign(Signer signer, Object intent) {
        byte[] bytes = CanonicalJson.toSignableBytes(intent);
        try {
            SignatureInfo signature = signer.sign(bytes);
            Validation.signature(signature);
            return signature;
        } catch (Exception error) {
            throw new A4pException(A4P_SIGNING_FAIL);
        }
    }

    private static VerificationResult verify(SignatureVerifier verifier, Object intent, SignatureInfo signature) {
        byte[] bytes = CanonicalJson.toSignableBytes(intent);
        try {
            VerificationResult result = verifier.verify(bytes, signature);
            // valid and code must agree; an inconsistent result is not trusted either way.
            if (result == null || result.valid() != (result.code() == A4P_SUCCESS.code())) {
                throw new A4pException(A4P_VERIFY_FAIL);
            }
            return result;
        } catch (Exception error) {
            throw new A4pException(A4P_VERIFY_FAIL);
        }
    }

    private static void verifyOrThrow(SignatureVerifier verifier, Object intent, SignatureInfo signature) {
        require(verify(verifier, intent, signature).valid(), A4P_VERIFY_FAIL);
    }
}
