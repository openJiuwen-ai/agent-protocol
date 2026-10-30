package io.openjiuwen.a4p;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import io.openjiuwen.a4p.Models.*;
import java.util.ArrayList;
import java.util.List;

/**
 * Maintains an authorized L3 intent, activation time, execution ledger and quota snapshot.
 * Obtain contexts through activation or a validated codec. Persist after every operation returning a context,
 * including failed reservations that mark it EXPIRED. Applications must serialize read-update-write cycles
 * for the same intent in a database transaction; in-memory synchronization alone is insufficient.
 * Quota snapshots change on execution operations, not on reads. Context JSON is for local persistence only
 * and requires application-level integrity protection where needed.
 * @see A4p
 */
public final class IntentExecutionContext {
    /** Authorized L3 snapshot bound to this context. */
    private final IntentL3 authorizedIntent;
    /** Activation instant in Unix epoch milliseconds. */
    private final long activatedTime;
    /** Decoded payment resource whitelist. */
    private final ShoppingItems resources;
    /** Decoded payment constraints. */
    private final PaymentConstraints constraints;
    /** Current intent execution state. */
    private IntentExecutionStatus status = IntentExecutionStatus.ACTIVE;
    /** Mutable ledger of reserved and finalized executions. */
    private final List<Entry> ledger = new ArrayList<>();
    /** Quota snapshot for the most recent accounting evaluation. */
    private PaymentExecutionInfo executionInfo;

    // A ledger record together with its parsed AI_PAY order.
    /**
     * Pairs an execution ledger record with its decoded payment order.
     * @param record execution ledger record
     * @param order payment order
     */
    record Entry(ExecutionRecord record, Order order) {}

    /**
     * Initializes an execution context from an authorized L3 intent and activation time.
     * Restoration overloads retain the supplied ledger and recompute the stored accounting period.
     * @param authorizedIntent authorized L3 snapshot
     * @param now trusted non-negative Unix epoch milliseconds within the safe integer range
     */
    IntentExecutionContext(IntentL3 authorizedIntent, long now) {
        this(now, authorizedIntent);
        Accounting.recompute(this, now);
    }

    // Restores a persisted context; executionInfo is recomputed for the period it was last computed in.
    /**
     * Initializes an execution context from an authorized L3 intent and activation time.
     * Restoration overloads retain the supplied ledger and recompute the stored accounting period.
     * @param authorizedIntent authorized L3 snapshot
     * @param status context execution state
     * @param ledger execution records paired with decoded orders
     * @param period accounting period index, or null when periodic limits are absent
     * @param activatedTime activation instant in Unix epoch milliseconds
     */
    IntentExecutionContext(IntentL3 authorizedIntent, IntentExecutionStatus status, List<Entry> ledger,
                           Long period, long activatedTime) {
        this(activatedTime, authorizedIntent);
        this.status = status;
        this.ledger.addAll(ledger);
        this.executionInfo = Accounting.executionInfo(this, period);
    }

    /**
     * Initializes an execution context from an authorized L3 intent and activation time.
     * Restoration overloads retain the supplied ledger and recompute the stored accounting period.
     * @param activatedTime activation instant in Unix epoch milliseconds
     * @param authorizedIntent authorized L3 snapshot
     */
    private IntentExecutionContext(long activatedTime, IntentL3 authorizedIntent) {
        this.activatedTime = activatedTime;
        this.authorizedIntent = authorizedIntent;
        StructuredIntent intent = authorizedIntent.intentL2().intentL1().structuredIntent();
        this.resources = PaymentProfile.resources(intent);
        this.constraints = PaymentProfile.constraints(intent);
    }

    /**
     * Returns the activation instant in epoch milliseconds, preserved across serialization.
     * @return activation instant in epoch milliseconds
     */
    public long activatedTime() { return activatedTime; }

    /**
     * Returns the supported protocol version.
     * @return protocol version
     */
    public String version() { return A4p.PROTOCOL_VERSION; }
    /**
     * Returns the identifier of the authorized intent.
     * @return associated intent identifier
     */
    public String intentId() { return authorizedIntent.intentL2().intentL1().intentId(); }
    /**
     * Returns the authorized L3 snapshot associated with this context.
     * @return authorized L3 snapshot
     */
    public IntentL3 authorizedIntent() { return authorizedIntent; }
    /**
     * Returns the current execution status without evaluating expiry.
     * @return current context state
     */
    public synchronized IntentExecutionStatus status() { return status; }
    /**
     * Returns an immutable snapshot of the execution ledger, including canceled records.
     * @return immutable ledger snapshot
     */
    public synchronized List<ExecutionRecord> executions() { return ledger.stream().map(Entry::record).toList(); }

    // Typed union: the action selects the schema; read it as PaymentExecutionInfo via PaymentProfile.
    /**
     * Returns a defensive JSON snapshot of remaining quotas, omitting absent optional figures.
     * Reading does not advance the accounting period.
     * @return quota snapshot without advancing its period
     */
    public synchronized JsonNode executionInfo() {
        ObjectNode fields = CanonicalJson.MAPPER.createObjectNode();
        fields.put("leftAmount", executionInfo.leftAmount());
        if (executionInfo.leftExecutionCount() != null) {
            fields.put("leftExecutionCount", executionInfo.leftExecutionCount());
        }
        if (executionInfo.periodIndex() != null) {
            fields.put("periodIndex", executionInfo.periodIndex());
        }
        if (executionInfo.leftPeriodExecutionCount() != null) {
            fields.put("leftPeriodExecutionCount", executionInfo.leftPeriodExecutionCount());
        }
        return fields;
    }

    /**
     * Returns the current typed payment quota snapshot without advancing its period.
     * @return current typed quota snapshot
     */
    synchronized PaymentExecutionInfo paymentExecutionInfo() { return executionInfo; }
    /**
     * Replaces the internal context state; callers must hold the context lock.
     * @param status context execution state
     */
    void status(IntentExecutionStatus status) { this.status = status; }
    /**
     * Replaces the internal quota snapshot; callers must hold the context lock.
     * @param info quota accounting snapshot
     */
    void executionInfo(PaymentExecutionInfo info) { this.executionInfo = info; }
    /**
     * Returns the mutable internal ledger; callers must coordinate context access.
     * @return mutable internal ledger
     */
    List<Entry> ledger() { return ledger; }
    /**
     * Returns the payment resources decoded from the authorized intent.
     * @return decoded payment resources
     */
    ShoppingItems resources() { return resources; }
    /**
     * Returns the payment constraints decoded from the authorized intent.
     * @return decoded payment constraints
     */
    PaymentConstraints constraints() { return constraints; }
}
