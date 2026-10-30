package io.openjiuwen.a4p;

import io.openjiuwen.a4p.Models.*;
import java.util.List;
import java.util.UUID;
import static io.openjiuwen.a4p.A4pErrorCode.*;
import static io.openjiuwen.a4p.Validation.require;

/**
 * Provides the A4P authorization lifecycle and deterministic payment execution operations.
 * Create L1, issue L2, sign L3, activate a context, then reserve and finalize executions.
 * Create and restore lifecycle objects through SDK APIs rather than assembling them by hand.
 * Do not repeat lifecycle transitions or finalize an execution twice.
 * Applications must persist returned contexts, including a failed reservation that marks a context EXPIRED,
 * and serialize updates to each intent in their database transaction.
 * All timestamps are Unix epoch milliseconds within the JavaScript safe integer range.
 * Explicit clocks must be trusted; overloads without a clock use System.currentTimeMillis().
 * Protocol failures are returned as A4pResult; unexpected SDK defects may throw A4pException.
 * @see A4pIntentCodec
 */
public final class A4p {
    /** A4P Core version supported by this SDK. */
    public static final String PROTOCOL_VERSION = "1.0";

    /**
     * Prevents instantiation of this utility class.
     */
    private A4p() {}

    /**
     * Validates a creation request and signs the resulting L1 with the agent key.
     * Generates a UUID when intentId is omitted. The agent must filter sensitive conversation data
     * and enforce conversation length limits before calling this method.
     * Uses the system clock.
     * @param request creation request including the conversation, summary and structured intent
     * @param agentSigner agent signing callback
     * @return signed L1 or a validation/signing failure
     */
    public static A4pResult<IntentL1> createIntent(CreateIntentRequest request, Signer agentSigner) {
        return createIntent(request, agentSigner, System.currentTimeMillis());
    }

    /**
     * Validates a creation request and signs the resulting L1 with the agent key.
     * Generates a UUID when intentId is omitted. The agent must filter sensitive conversation data
     * and enforce conversation length limits before calling this method.
     * Uses the supplied trusted evaluation instant.
     * @param request creation request including the conversation, summary and structured intent
     * @param agentSigner agent signing callback
     * @param now trusted non-negative Unix epoch milliseconds within the safe integer range
     * @return signed L1 or a validation/signing failure
     */
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

    /**
     * Validates L1 and its agent signature, binds trusted identities, and signs L2 with the intent center key.
     * The issuance time must not precede the L1 timestamp; an earlier time returns A4P_INVALID_PARAMETER.
     * Uses the system clock.
     * @param intent protocol intent to process
     * @param agentVerifier verifier for the agent signature
     * @param context trusted identities bound into L2
     * @param serverSigner intent center signing callback
     * @return signed L2 or a validation/verification/signing failure
     */
    public static A4pResult<IntentL2> issueIntent(IntentL1 intent, SignatureVerifier agentVerifier,
                                                 IntentIssueContext context, Signer serverSigner) {
        return issueIntent(intent, agentVerifier, context, serverSigner, System.currentTimeMillis());
    }

    /**
     * Validates L1 and its agent signature, binds trusted identities, and signs L2 with the intent center key.
     * The issuance time must not precede the L1 timestamp; an earlier time returns A4P_INVALID_PARAMETER.
     * Uses the supplied trusted evaluation instant.
     * @param intent protocol intent to process
     * @param agentVerifier verifier for the agent signature
     * @param context trusted identities bound into L2
     * @param serverSigner intent center signing callback
     * @param now trusted non-negative Unix epoch milliseconds within the safe integer range
     * @return signed L2 or a validation/verification/signing failure
     */
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

    /**
     * Validates L2 and L1 versions, verifies the intent center signature, and user-signs L3 with the signing environment.
     * Before verification or signing, requires now at or after L2.timestamp and at or before validUntil.
     * An earlier time returns A4P_INVALID_PARAMETER; expiration returns A4P_TIMESTAMP_EXPIRED.
     * Signing before validFrom is allowed; reservations before validFrom are rejected.
     * Uses the system clock.
     * @param intent protocol intent to process
     * @param serverVerifier verifier for the intent center signature
     * @param environment device and application signing environment
     * @param userSigner user-device signing callback
     * @return user-signed L3 or a protocol failure
     */
    public static A4pResult<IntentL3> signIntent(IntentL2 intent, SignatureVerifier serverVerifier,
                                                SigningEnv environment, Signer userSigner) {
        return signIntent(intent, serverVerifier, environment, userSigner, System.currentTimeMillis());
    }

    /**
     * Validates L2 and L1 versions, verifies the intent center signature, and user-signs L3 with the signing environment.
     * Before verification or signing, requires now at or after L2.timestamp and at or before validUntil.
     * An earlier time returns A4P_INVALID_PARAMETER; expiration returns A4P_TIMESTAMP_EXPIRED.
     * Signing before validFrom is allowed; reservations before validFrom are rejected.
     * Uses the supplied trusted evaluation instant.
     * @param intent protocol intent to process
     * @param serverVerifier verifier for the intent center signature
     * @param environment device and application signing environment
     * @param userSigner user-device signing callback
     * @param now trusted non-negative Unix epoch milliseconds within the safe integer range
     * @return user-signed L3 or a protocol failure
     */
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

    /**
     * Validates the L3 chain and user signature and creates an ACTIVE context with its activation time.
     * Activation before L3.timestamp returns A4P_INVALID_PARAMETER; after validUntil it returns A4P_TIMESTAMP_EXPIRED.
     * Activation before validFrom is allowed, but execution must wait. Persist the resulting context.
     * Uses the system clock.
     * @param intent protocol intent to process
     * @param userVerifier verifier for the user signature
     * @return new ACTIVE context or a protocol failure
     */
    public static A4pResult<IntentExecutionContext> activateIntent(IntentL3 intent, SignatureVerifier userVerifier) {
        return activateIntent(intent, userVerifier, System.currentTimeMillis());
    }

    /**
     * Validates the L3 chain and user signature and creates an ACTIVE context with its activation time.
     * Activation before L3.timestamp returns A4P_INVALID_PARAMETER; after validUntil it returns A4P_TIMESTAMP_EXPIRED.
     * Activation before validFrom is allowed, but execution must wait. Persist the resulting context.
     * Uses the supplied trusted evaluation instant.
     * @param intent protocol intent to process
     * @param userVerifier verifier for the user signature
     * @param now trusted non-negative Unix epoch milliseconds within the safe integer range
     * @return new ACTIVE context or a protocol failure
     */
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

    /**
     * Checks chain structure, protocol versions and the L2 intent center signature.
     * Does not verify the user signature, inspect execution status or establish current execution eligibility.
     * A successful SDK call may contain a negative verification verdict; inspect the returned VerificationResult.
     * @param intent protocol intent to process
     * @param issuerVerifier verifier for the intent center signature
     * @return verification verdict or a protocol failure; inspect both SDK success and verdict validity
     */
    public static A4pResult<VerificationResult> verifyIntentAuthenticity(IntentL3 intent,
                                                                        SignatureVerifier issuerVerifier) {
        return A4pResult.capture(() -> {
            Validation.intentL3(intent);
            Validation.versions(intent);
            return verify(issuerVerifier, intent.intentL2(), intent.intentL2().sign());
        });
    }

    /**
     * Changes an ACTIVE context to REVOKED without restricting revocation to the authorization validity window.
     * Evaluation before activation returns A4P_TIMESTAMP_TOO_EARLY. Pending executions may still be finalized.
     * Persist the updated context and do not repeat revocation.
     * Uses the system clock.
     * @param context activated intent and mutable execution ledger
     * @return updated context or a protocol failure
     */
    public static A4pResult<IntentExecutionContext> revokeIntent(IntentExecutionContext context) {
        return revokeIntent(context, System.currentTimeMillis());
    }

    /**
     * Changes an ACTIVE context to REVOKED without restricting revocation to the authorization validity window.
     * Evaluation before activation returns A4P_TIMESTAMP_TOO_EARLY. Pending executions may still be finalized.
     * Persist the updated context and do not repeat revocation.
     * Uses the supplied trusted evaluation instant.
     * @param context activated intent and mutable execution ledger
     * @param now trusted non-negative Unix epoch milliseconds within the safe integer range
     * @return updated context or a protocol failure
     */
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

    /**
     * Checks status, validity, action, currency, unique IDs, cart shape and payment quotas, then reserves a PENDING execution.
     * Omitted execution IDs are generated. Order and execution IDs remain unavailable for reuse after cancellation.
     * PENDING and COMPLETED records occupy quota. The trusted clock determines the reservation period, not the order timestamp.
     * An expired reservation attempt can mark the context EXPIRED even when the result is a failure; persist it in either case.
     * Uses the system clock.
     * @param context activated intent and mutable execution ledger
     * @param execution action-specific execution request
     * @return updated context or a protocol failure; failure may still mutate context status
     */
    public static A4pResult<IntentExecutionContext> createExecution(IntentExecutionContext context,
                                                                    Execution execution) {
        return createExecution(context, execution, System.currentTimeMillis());
    }

    /**
     * Checks status, validity, action, currency, unique IDs, cart shape and payment quotas, then reserves a PENDING execution.
     * Omitted execution IDs are generated. Order and execution IDs remain unavailable for reuse after cancellation.
     * PENDING and COMPLETED records occupy quota. The trusted clock determines the reservation period, not the order timestamp.
     * An expired reservation attempt can mark the context EXPIRED even when the result is a failure; persist it in either case.
     * Uses the supplied trusted evaluation instant.
     * @param context activated intent and mutable execution ledger
     * @param execution action-specific execution request
     * @param now trusted non-negative Unix epoch milliseconds within the safe integer range
     * @return updated context or a protocol failure; failure may still mutate context status
     */
    public static A4pResult<IntentExecutionContext> createExecution(IntentExecutionContext context,
                                                                    Execution execution, long now) {
        return A4pResult.capture(() -> reserve(context, execution, now));
    }

    /**
     * Changes a PENDING execution to COMPLETED while retaining its occupied quotas.
     * Refreshes the accounting snapshot for the evaluation period without moving the reservation period.
     * An ACTIVE context becomes CONSUMED only when global quota is exhausted and no execution is pending.
     * REVOKED and EXPIRED contexts may finalize existing reservations and retain their status.
     * Evaluation must not precede reservation. Persist the context and never finalize the same record twice.
     * Uses the system clock.
     * @param context activated intent and mutable execution ledger
     * @param executionId execution identifier, retained even after cancellation
     * @return updated context or a protocol failure
     */
    public static A4pResult<IntentExecutionContext> completeExecution(IntentExecutionContext context,
                                                                      String executionId) {
        return completeExecution(context, executionId, System.currentTimeMillis());
    }

    /**
     * Changes a PENDING execution to COMPLETED while retaining its occupied quotas.
     * Refreshes the accounting snapshot for the evaluation period without moving the reservation period.
     * An ACTIVE context becomes CONSUMED only when global quota is exhausted and no execution is pending.
     * REVOKED and EXPIRED contexts may finalize existing reservations and retain their status.
     * Evaluation must not precede reservation. Persist the context and never finalize the same record twice.
     * Uses the supplied trusted evaluation instant.
     * @param context activated intent and mutable execution ledger
     * @param executionId execution identifier, retained even after cancellation
     * @param now trusted non-negative Unix epoch milliseconds within the safe integer range
     * @return updated context or a protocol failure
     */
    public static A4pResult<IntentExecutionContext> completeExecution(IntentExecutionContext context,
                                                                      String executionId, long now) {
        return A4pResult.capture(() -> finalizeExecution(context, executionId, now, false));
    }

    /**
     * Changes a PENDING execution to CANCELED, releases its quota and retains the ledger record and IDs.
     * Refreshes accounting for the evaluation period; canceling an old-period order does not change current-period usage.
     * REVOKED and EXPIRED contexts may cancel existing reservations. Evaluation must not precede reservation.
     * Persist the context and never finalize the same record twice.
     * Uses the system clock.
     * @param context activated intent and mutable execution ledger
     * @param executionId execution identifier, retained even after cancellation
     * @return updated context or a protocol failure
     */
    public static A4pResult<IntentExecutionContext> cancelExecution(IntentExecutionContext context,
                                                                    String executionId) {
        return cancelExecution(context, executionId, System.currentTimeMillis());
    }

    /**
     * Changes a PENDING execution to CANCELED, releases its quota and retains the ledger record and IDs.
     * Refreshes accounting for the evaluation period; canceling an old-period order does not change current-period usage.
     * REVOKED and EXPIRED contexts may cancel existing reservations. Evaluation must not precede reservation.
     * Persist the context and never finalize the same record twice.
     * Uses the supplied trusted evaluation instant.
     * @param context activated intent and mutable execution ledger
     * @param executionId execution identifier, retained even after cancellation
     * @param now trusted non-negative Unix epoch milliseconds within the safe integer range
     * @return updated context or a protocol failure
     */
    public static A4pResult<IntentExecutionContext> cancelExecution(IntentExecutionContext context,
                                                                    String executionId, long now) {
        return A4pResult.capture(() -> finalizeExecution(context, executionId, now, true));
    }

    /**
     * Validates and appends a pending payment reservation under the context lock.
     * Checks expiry before duplicate IDs and may mark the context EXPIRED before throwing.
     * @param context activated intent and mutable execution ledger
     * @param execution action-specific execution request
     * @param now trusted non-negative Unix epoch milliseconds within the safe integer range
     * @return context containing the pending reservation
     */
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

    /**
     * Completes or cancels a pending record under the context lock and refreshes quota accounting.
     * Preserves terminal context states and changes an exhausted ACTIVE context to CONSUMED only after completion.
     * @param context activated intent and mutable execution ledger
     * @param executionId execution identifier, retained even after cancellation
     * @param now trusted non-negative Unix epoch milliseconds within the safe integer range
     * @param cancel true to cancel and release quota, false to complete
     * @return context containing the finalized record
     */
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

    /**
     * Requires one cart entry per authorized group with exactly the authorized quantity.
     * @param context activated intent and mutable execution ledger
     * @param items cart entries to validate
     */
    private static void enforceWhitelist(IntentExecutionContext context, List<CartItem> items) {
        List<ItemWhiteList> groups = context.resources().itemsWhiteList();
        CartMatching.requireOneToOne(items, groups);
    }

    /**
     * Signs canonical bytes excluding the outer signature and validates returned signature metadata.
     * Maps signer exceptions and invalid metadata to A4P_SIGNING_FAIL.
     * @param signer application signing callback
     * @param intent protocol intent to process
     * @return signature metadata
     */
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

    /**
     * Verifies canonical bytes and requires the validity flag to agree with a zero result code.
     * Maps callback exceptions and inconsistent verdicts to A4P_VERIFY_FAIL.
     * @param verifier application verification callback
     * @param intent protocol intent to process
     * @param signature signature metadata to verify
     * @return signature verification verdict
     */
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

    /**
     * Requires a positive signature verdict and throws A4P_VERIFY_FAIL otherwise.
     * @param verifier application verification callback
     * @param intent protocol intent to process
     * @param signature signature metadata to verify
     */
    private static void verifyOrThrow(SignatureVerifier verifier, Object intent, SignatureInfo signature) {
        require(verify(verifier, intent, signature).valid(), A4P_VERIFY_FAIL);
    }
}
