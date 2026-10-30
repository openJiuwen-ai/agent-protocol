package io.openjiuwen.a4p;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import io.openjiuwen.a4p.Models.*;
import java.util.ArrayList;
import java.util.List;

public final class IntentExecutionContext {
    private final IntentL3 authorizedIntent;
    private final long activatedTime;
    private final ShoppingItems resources;
    private final PaymentConstraints constraints;
    private IntentExecutionStatus status = IntentExecutionStatus.ACTIVE;
    private final List<Entry> ledger = new ArrayList<>();
    private PaymentExecutionInfo executionInfo;

    // A ledger record together with its parsed AI_PAY order.
    record Entry(ExecutionRecord record, Order order) {}

    IntentExecutionContext(IntentL3 authorizedIntent, long now) {
        this(now, authorizedIntent);
        Accounting.recompute(this, now);
    }

    // Restores a persisted context; executionInfo is recomputed for the period it was last computed in.
    IntentExecutionContext(IntentL3 authorizedIntent, IntentExecutionStatus status, List<Entry> ledger,
                           Long period, long activatedTime) {
        this(activatedTime, authorizedIntent);
        this.status = status;
        this.ledger.addAll(ledger);
        this.executionInfo = Accounting.executionInfo(this, period);
    }

    private IntentExecutionContext(long activatedTime, IntentL3 authorizedIntent) {
        this.activatedTime = activatedTime;
        this.authorizedIntent = authorizedIntent;
        StructuredIntent intent = authorizedIntent.intentL2().intentL1().structuredIntent();
        this.resources = PaymentProfile.resources(intent);
        this.constraints = PaymentProfile.constraints(intent);
    }

    /** Activation instant in epoch milliseconds, preserved by the context codec. */
    public long activatedTime() { return activatedTime; }

    public String version() { return A4p.PROTOCOL_VERSION; }
    public String intentId() { return authorizedIntent.intentL2().intentL1().intentId(); }
    public IntentL3 authorizedIntent() { return authorizedIntent; }
    public synchronized IntentExecutionStatus status() { return status; }
    public synchronized List<ExecutionRecord> executions() { return ledger.stream().map(Entry::record).toList(); }

    // Typed union: the action selects the schema; read it as PaymentExecutionInfo via PaymentProfile.
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

    synchronized PaymentExecutionInfo paymentExecutionInfo() { return executionInfo; }
    void status(IntentExecutionStatus status) { this.status = status; }
    void executionInfo(PaymentExecutionInfo info) { this.executionInfo = info; }
    List<Entry> ledger() { return ledger; }
    ShoppingItems resources() { return resources; }
    PaymentConstraints constraints() { return constraints; }
}
