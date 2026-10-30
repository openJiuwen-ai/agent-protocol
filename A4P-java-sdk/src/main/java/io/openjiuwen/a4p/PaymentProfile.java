package io.openjiuwen.a4p;

import com.fasterxml.jackson.annotation.JsonInclude;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.json.JsonMapper;
import io.openjiuwen.a4p.Models.*;
import static io.openjiuwen.a4p.A4pErrorCode.*;

// Payment Profile of the typed union for action AI_PAY (API spec section 4.5).
public final class PaymentProfile {
    // Absent optional fields are omitted, matching the canonical form the ArkTS peer signs.
    private static final JsonMapper PAYLOAD_MAPPER = CanonicalJson.MAPPER.rebuild()
            .defaultPropertyInclusion(JsonInclude.Value.construct(JsonInclude.Include.NON_NULL,
                    JsonInclude.Include.NON_NULL))
            .build();

    private PaymentProfile() {}

    public static StructuredIntent structuredIntent(ShoppingItems resources, PaymentConstraints constraints) {
        return new StructuredIntent(IntentActionType.AI_PAY, toNode(resources), toNode(constraints));
    }

    public static Execution execution(String id, Order order) {
        return new Execution(IntentActionType.AI_PAY, id, toNode(order));
    }

    public static ShoppingItems resources(StructuredIntent intent) {
        Validation.present(intent);
        requireAction(intent.action());
        return parse(intent.resources(), ShoppingItems.class);
    }

    public static PaymentConstraints constraints(StructuredIntent intent) {
        Validation.present(intent);
        requireAction(intent.action());
        return parse(intent.constraints(), PaymentConstraints.class);
    }

    public static Order order(Execution execution) {
        Validation.present(execution);
        requireAction(execution.action());
        return parse(execution.executionContent(), Order.class);
    }

    public static PaymentExecutionInfo executionInfo(IntentExecutionContext context) {
        Validation.present(context);
        return context.paymentExecutionInfo();
    }

    private static void requireAction(IntentActionType action) {
        Validation.require(action == IntentActionType.AI_PAY, A4P_INVALID_PARAMETER);
    }

    private static JsonNode toNode(Object value) {
        return value == null ? null : CanonicalJson.freezeNode(PAYLOAD_MAPPER.valueToTree(value));
    }

    // Explicit nulls are rejected like the ArkTS validators do; absent fields are the only way to omit a value.
    private static <Value> Value parse(JsonNode node, Class<Value> type) {
        Validation.require(node != null && !containsNull(node), A4P_INVALID_PARAMETER);
        try {
            return CanonicalJson.MAPPER.treeToValue(node, type);
        } catch (Exception error) {
            throw new A4pException(A4P_INVALID_PARAMETER);
        }
    }

    private static boolean containsNull(JsonNode node) {
        if (node.isNull()) { return true; }
        for (JsonNode child : node) {
            if (containsNull(child)) { return true; }
        }
        return false;
    }
}
