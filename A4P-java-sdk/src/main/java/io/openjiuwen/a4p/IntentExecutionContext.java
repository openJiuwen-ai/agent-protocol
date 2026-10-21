package io.openjiuwen.a4p;

import com.fasterxml.jackson.databind.JsonNode;
import io.openjiuwen.a4p.Models.*;
import java.util.ArrayList;
import java.util.List;

public final class IntentExecutionContext {
    private final IntentL3 authorizedIntent;
    private final ShoppingItems resources;
    private final PaymentConstraints constraints;
    private IntentExecutionStatus status = IntentExecutionStatus.ACTIVE;
    private final List<Entry> ledger = new ArrayList<>();
    private PaymentExecutionInfo executionInfo;

    // A ledger record together with its parsed AI_PAY order.
    record Entry(ExecutionRecord record, Order order) {}

    IntentExecutionContext(IntentL3 authorizedIntent, long now) {
        this(authorizedIntent);
        Accounting.recompute(this, now);
    }

    // Restores a persisted context; executionInfo is recomputed for the period it was last computed in.
    IntentExecutionContext(IntentL3 authorizedIntent, IntentExecutionStatus status, List<Entry> ledger,
                           Long period) {
        this(authorizedIntent);
        this.status = status;
        this.ledger.addAll(ledger);
        this.executionInfo = Accounting.executionInfo(this, period);
    }

    private IntentExecutionContext(IntentL3 authorizedIntent) {
        this.authorizedIntent = authorizedIntent;
        StructuredIntent intent = authorizedIntent.intentL2().intentL1().structuredIntent();
        this.resources = PaymentProfile.resources(intent);
        this.constraints = PaymentProfile.constraints(intent);
    }

    public String version() { return A4p.PROTOCOL_VERSION; }
    public String intentId() { return authorizedIntent.intentL2().intentL1().intentId(); }
    public IntentL3 authorizedIntent() { return authorizedIntent; }
    public synchronized IntentExecutionStatus status() { return status; }
    public synchronized List<ExecutionRecord> executions() { return ledger.stream().map(Entry::record).toList(); }

    // Typed union: the action selects the schema; read it as PaymentExecutionInfo via PaymentProfile.
    public synchronized JsonNode executionInfo() {
        return CanonicalJson.freezeNode(CanonicalJson.MAPPER.valueToTree(executionInfo));
    }

    synchronized PaymentExecutionInfo paymentExecutionInfo() { return executionInfo; }
    void status(IntentExecutionStatus status) { this.status = status; }
    void executionInfo(PaymentExecutionInfo info) { this.executionInfo = info; }
    List<Entry> ledger() { return ledger; }
    ShoppingItems resources() { return resources; }
    PaymentConstraints constraints() { return constraints; }
}
