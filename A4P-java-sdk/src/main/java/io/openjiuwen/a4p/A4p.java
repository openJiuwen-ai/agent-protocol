package io.openjiuwen.a4p;

import java.util.List;
import java.util.UUID;

import static io.openjiuwen.a4p.A4pErrorCode.A4P_AMOUNT_EXCEEDED;
import static io.openjiuwen.a4p.A4pErrorCode.A4P_DUPLICATE_ID;
import static io.openjiuwen.a4p.A4pErrorCode.A4P_EXECUTION_COUNT_EXCEEDED;
import static io.openjiuwen.a4p.A4pErrorCode.A4P_INVALID_PARAMETER;
import static io.openjiuwen.a4p.A4pErrorCode.A4P_INVALID_STATUS;
import static io.openjiuwen.a4p.A4pErrorCode.A4P_PERIOD_EXECUTION_COUNT_EXCEEDED;
import static io.openjiuwen.a4p.A4pErrorCode.A4P_SIGNING_FAIL;
import static io.openjiuwen.a4p.A4pErrorCode.A4P_SINGLE_ORDER_AMOUNT_EXCEEDED;
import static io.openjiuwen.a4p.A4pErrorCode.A4P_SUCCESS;
import static io.openjiuwen.a4p.A4pErrorCode.A4P_TIMESTAMP_EXPIRED;
import static io.openjiuwen.a4p.A4pErrorCode.A4P_TIMESTAMP_TOO_EARLY;
import static io.openjiuwen.a4p.A4pErrorCode.A4P_VERIFY_FAIL;
import io.openjiuwen.a4p.Models.CartItem;
import io.openjiuwen.a4p.Models.CreateIntentRequest;
import io.openjiuwen.a4p.Models.Execution;
import io.openjiuwen.a4p.Models.ExecutionRecord;
import io.openjiuwen.a4p.Models.ExecutionStatus;
import io.openjiuwen.a4p.Models.IntentExecutionStatus;
import io.openjiuwen.a4p.Models.IntentIssueContext;
import io.openjiuwen.a4p.Models.IntentL1;
import io.openjiuwen.a4p.Models.IntentL2;
import io.openjiuwen.a4p.Models.IntentL3;
import io.openjiuwen.a4p.Models.ItemWhiteList;
import io.openjiuwen.a4p.Models.Order;
import io.openjiuwen.a4p.Models.PaymentConstraints;
import io.openjiuwen.a4p.Models.SignatureInfo;
import io.openjiuwen.a4p.Models.SigningEnv;
import io.openjiuwen.a4p.Models.VerificationResult;
import static io.openjiuwen.a4p.Validation.require;

/**
 * Provides the A4P authorization lifecycle and deterministic payment execution operations.
 * Create L1, issue L2, sign L3, activate a context, then reserve and finalize executions.
 * Create and restore lifecycle objects through SDK APIs rather than assembling them by hand.
 * Do not repeat lifecycle transitions or finalize an execution twice.
 * Applications must persist returned contexts, including a failed reservation that marks a context EXPIRED,
 * and serialize updates to each intent in their database transaction.
 * All timestamps are Unix epoch milliseconds within the JavaScript safe integer range.
 * Lifecycle operations require an explicit now argument.
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
     * Validates a creation request, requires now at or before validUntil, and signs L1 with the agent key.
     * Generates a UUID when intentId is omitted. The agent must filter sensitive conversation data
     * and enforce conversation length limits before calling this method.
     * Uses the supplied evaluation instant.
     * @param request creation request including the conversation, summary and structured intent
     * @param agentSigner agent signing callback
     * @param now non-negative Unix epoch milliseconds within the safe integer range
     * @return signed L1 or a validation/signing failure
     */
    public static A4pResult<IntentL1> createIntent(CreateIntentRequest request, Signer agentSigner, long now) {
        return A4pResult.capture(() -> {
            Validation.integer(now, false);
            Validation.request(request);
            // Require now at or before validUntil.
            Validation.timeAtOrBefore(now, PaymentProfile.constraints(request.structuredIntent()).validUntil(), A4P_TIMESTAMP_EXPIRED, "Timestamp is later than validUntil");
            String intentId = request.intentId() == null ? UUID.randomUUID().toString() : request.intentId();
            IntentL1 unsigned = new IntentL1(PROTOCOL_VERSION, intentId, request.intentConversation(),
                    request.intentSummary(), request.structuredIntent(), now, null);
            return new IntentL1(unsigned.version(), intentId, unsigned.intentConversation(), unsigned.intentSummary(),
                    unsigned.structuredIntent(), now, sign(agentSigner, unsigned));
        });
    }

    /**
     * Validates L1 and its agent signature, binds trusted identities, and signs L2 with the intent center key.
     * Requires now at or before validUntil; does not compare now with L1.timestamp.
     * Uses the supplied evaluation instant.
     * @param intent protocol intent to process
     * @param agentVerifier verifier for the agent signature
     * @param context trusted identities bound into L2
     * @param serverSigner intent center signing callback
     * @param now non-negative Unix epoch milliseconds within the safe integer range
     * @return signed L2 or a validation/verification/signing failure
     */
    public static A4pResult<IntentL2> issueIntent(IntentL1 intent, SignatureVerifier agentVerifier,
                                                 IntentIssueContext context, Signer serverSigner, long now) {
        return A4pResult.capture(() -> {
            Validation.integer(now, false);
            Validation.issueContext(context);
            Validation.intentL1(intent);
            Validation.version(intent.version());
            // Require now at or before validUntil.
            Validation.timeAtOrBefore(now, PaymentProfile.constraints(intent.structuredIntent()).validUntil(), A4P_TIMESTAMP_EXPIRED, "Timestamp is later than validUntil");
            verifyOrThrow(agentVerifier, intent, intent.sign());
            IntentL2 unsigned = new IntentL2(PROTOCOL_VERSION, intent, context.userIdentity(),
                    context.agentIdentity(), context.serviceProviderIdentity(), now, null);
            return new IntentL2(unsigned.version(), intent, unsigned.userIdentity(), unsigned.agentIdentity(),
                    unsigned.serviceProviderIdentity(), now, sign(serverSigner, unsigned));
        });
    }

    /**
     * Validates L2 and L1 versions, verifies the intent center signature, and user-signs L3 with the signing environment.
     * Does not compare now with L2.timestamp or validUntil.
     * Signing before validFrom is allowed; reservations before validFrom are rejected.
     * Uses the supplied evaluation instant.
     * @param intent protocol intent to process
     * @param serverVerifier verifier for the intent center signature
     * @param environment device and application signing environment
     * @param userSigner user-device signing callback
     * @param now non-negative Unix epoch milliseconds within the safe integer range
     * @return user-signed L3 or a protocol failure
     */
    public static A4pResult<IntentL3> signIntent(IntentL2 intent, SignatureVerifier serverVerifier,
                                                SigningEnv environment, Signer userSigner, long now) {
        return A4pResult.capture(() -> {
            Validation.integer(now, false);
            Validation.signingEnv(environment);
            Validation.intentL2(intent);
            Validation.versions(intent);
            // Do not compare now with L2.timestamp or validUntil.
            verifyOrThrow(serverVerifier, intent, intent.sign());
            IntentL3 unsigned = new IntentL3(PROTOCOL_VERSION, intent, environment, now, null);
            return new IntentL3(PROTOCOL_VERSION, intent, environment, now, sign(userSigner, unsigned));
        });
    }

    /**
     * Validates the L3 chain and user signature and creates an ACTIVE context with its activation time.
     * Activation after validUntil returns A4P_TIMESTAMP_EXPIRED; audit timestamps are not compared.
     * Activation before validFrom is allowed, but execution must wait. Persist the resulting context.
     * Uses the supplied evaluation instant.
     * @param intent protocol intent to process
     * @param userVerifier verifier for the user signature
     * @param now non-negative Unix epoch milliseconds within the safe integer range
     * @return new ACTIVE context or a protocol failure
     */
    public static A4pResult<IntentExecutionContext> activateIntent(IntentL3 intent, SignatureVerifier userVerifier,
                                                                   long now) {
        return A4pResult.capture(() -> {
            Validation.integer(now, false);
            Validation.intentL3(intent);
            Validation.versions(intent);
            // An intent past validUntil could never execute; one not yet valid is activated and waits for validFrom.
            PaymentConstraints constraints = PaymentProfile.constraints(intent.intentL2().intentL1().structuredIntent());
            Validation.timeAtOrBefore(now, constraints.validUntil(), A4P_TIMESTAMP_EXPIRED, "Timestamp is later than validUntil");
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
     * Revocation time is audit-only. Pending executions may still be finalized.
     * Persist the updated context and do not repeat revocation.
     * Uses the supplied evaluation instant.
     * @param context activated intent and mutable execution ledger
     * @param now non-negative Unix epoch milliseconds within the safe integer range
     * @return updated context or a protocol failure
     */
    public static A4pResult<IntentExecutionContext> revokeIntent(IntentExecutionContext context, long now) {
        return A4pResult.capture(() -> {
            Validation.present(context);
            Validation.integer(now, false);
            synchronized (context) {
                require(context.status() == IntentExecutionStatus.ACTIVE, A4P_INVALID_STATUS, "Intent must be active for this operation");
                context.revokedTime(now);
                context.status(IntentExecutionStatus.REVOKED);
                return context;
            }
        });
    }

    /**
     * Checks status, validity, action, currency, unique IDs, cart shape and payment quotas, then reserves a PENDING execution.
     * Omitted execution IDs are generated. Order and execution IDs remain unavailable for reuse after cancellation.
     * PENDING and COMPLETED records occupy quota. The evaluation instant determines the reservation period.
     * An expired reservation attempt can mark the context EXPIRED even when the result is a failure; persist it in either case.
     * Uses the supplied evaluation instant.
     * @param context activated intent and mutable execution ledger
     * @param execution action-specific execution request
     * @param now non-negative Unix epoch milliseconds within the safe integer range
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
     * Uses the supplied evaluation instant.
     * @param context activated intent and mutable execution ledger
     * @param executionId execution identifier, retained even after cancellation
     * @param now non-negative Unix epoch milliseconds within the safe integer range
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
     * Uses the supplied evaluation instant.
     * @param context activated intent and mutable execution ledger
     * @param executionId execution identifier, retained even after cancellation
     * @param now non-negative Unix epoch milliseconds within the safe integer range
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
     * @param now non-negative Unix epoch milliseconds within the safe integer range
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
            require(execution.action() == intent.structuredIntent().action(), A4P_INVALID_PARAMETER, "Execution action does not match the authorized intent action");
            require(order.currency().equals(constraints.currency()), A4P_INVALID_PARAMETER, "Order currency does not match the intent currency");
            require(context.status() == IntentExecutionStatus.ACTIVE, A4P_INVALID_STATUS, "Intent must be active for this operation");
            Validation.timeAtOrAfter(now, constraints.validFrom(), A4P_TIMESTAMP_TOO_EARLY, "Timestamp is earlier than validFrom");
            if (now > constraints.validUntil()) {
                context.status(IntentExecutionStatus.EXPIRED);
                throw new A4pException(A4P_TIMESTAMP_EXPIRED);
            }
            require(context.ledger().stream().noneMatch(entry -> entry.record().executionId().equals(executionId)), A4P_DUPLICATE_ID, "Duplicate execution ID");
            // orderId is the payment idempotency key, so canceled records still block reuse.
            require(context.ledger().stream().noneMatch(entry -> entry.order().orderId().equals(order.orderId())), A4P_DUPLICATE_ID, "Duplicate order ID");
            if (constraints.maxExecutionCount() != null) {
                require(Accounting.count(context) < constraints.maxExecutionCount(), A4P_EXECUTION_COUNT_EXCEEDED, "Execution count exceeds the total limit");
            }
            if (constraints.singleOrderMaxAmount() != null) {
                require(order.amount() <= constraints.singleOrderMaxAmount(), A4P_SINGLE_ORDER_AMOUNT_EXCEEDED, "Order amount exceeds the single-order limit");
            }
            require(order.amount() <= Accounting.leftAmount(context), A4P_AMOUNT_EXCEEDED, "Order amounts exceed the total available budget");
            enforceWhitelist(context, order.cartItems());
            Long period = Accounting.period(constraints, now);
            if (period != null) {
                require(Accounting.periodCount(context, period) < constraints.maxExecutionCountPerPeriod(), A4P_PERIOD_EXECUTION_COUNT_EXCEEDED, "Execution count exceeds the per-period limit");
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
     * @param now non-negative Unix epoch milliseconds within the safe integer range
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
                    || context.status() == IntentExecutionStatus.EXPIRED, A4P_INVALID_STATUS, "Intent must be active, revoked or expired to finalize an execution");
            List<IntentExecutionContext.Entry> ledger = context.ledger();
            int index = -1;
            for (int position = 0; position < ledger.size(); position++) {
                if (ledger.get(position).record().executionId().equals(executionId)) {
                    index = position;
                    break;
                }
            }
            require(index >= 0, A4P_INVALID_PARAMETER, "Execution ID was not found");
            IntentExecutionContext.Entry entry = ledger.get(index);
            ExecutionRecord record = entry.record();
            require(record.status() == ExecutionStatus.PENDING, A4P_INVALID_STATUS, "Only pending execution records can be finalized");
            // Record timestamps are monotonic like the chain's, so a persisted ledger can be checked for them.
            Validation.timeAtOrAfter(now, record.reservedAt(), A4P_TIMESTAMP_TOO_EARLY, "Finalization time is earlier than reservation time");
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
     * Maps all signer callback exceptions to A4P_SIGNING_FAIL; invalid returned metadata maps to A4P_INVALID_PARAMETER.
     * @param signer application signing callback
     * @param intent protocol intent to process
     * @return signature metadata
     */
    private static SignatureInfo sign(Signer signer, Object intent) {
        byte[] bytes = CanonicalJson.toSignableBytes(intent);
        SignatureInfo signature;
        try {
            signature = signer.sign(bytes);
        } catch (Exception error) {
            throw new A4pException(A4P_SIGNING_FAIL, String.format("A4P_SIGNING_FAIL: %s", error.toString()), error);
        }
        Validation.signature(signature);
        return signature;
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
        require(verify(verifier, intent, signature).valid(), A4P_VERIFY_FAIL, "Intent signature verification failed");
    }
}
