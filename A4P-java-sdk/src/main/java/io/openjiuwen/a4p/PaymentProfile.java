package io.openjiuwen.a4p;

import com.fasterxml.jackson.annotation.JsonInclude;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.json.JsonMapper;
import io.openjiuwen.a4p.Models.*;
import static io.openjiuwen.a4p.A4pErrorCode.*;

// Payment Profile of the typed union for action AI_PAY (API spec section 4.5).
/**
 * Constructs and decodes the AI_PAY payloads of the action-selected protocol union.
 * Resources contain ShoppingItems, constraints contain PaymentConstraints, and execution content contains Order.
 * These helpers do not replace the validation performed by lifecycle operations.
 * Missing optional fields are omitted; explicit JSON null values are rejected when decoding payment payloads.
 * @see Models
 */
public final class PaymentProfile {
    // Absent optional fields are omitted, matching the canonical form the ArkTS peer signs.
    /** Payment mapper that omits absent optional fields from record payloads. */
    private static final JsonMapper PAYLOAD_MAPPER = CanonicalJson.MAPPER.rebuild()
            .defaultPropertyInclusion(JsonInclude.Value.construct(JsonInclude.Include.NON_NULL,
                    JsonInclude.Include.NON_NULL))
            .build();

    /**
     * Prevents instantiation of this utility class.
     */
    private PaymentProfile() {}

    /**
     * Wraps payment resources and constraints as an AI_PAY structured intent without performing lifecycle validation.
     * @param resources authorized AI_PAY shopping whitelist payload
     * @param constraints authorized AI_PAY payment limits
     * @return AI_PAY structured intent
     */
    public static StructuredIntent structuredIntent(ShoppingItems resources, PaymentConstraints constraints) {
        return new StructuredIntent(IntentActionType.AI_PAY, toNode(resources), toNode(constraints));
    }

    /**
     * Wraps an order as an AI_PAY execution request; a null ID requests ID generation during reservation.
     * @param id optional execution identifier; null requests generation during reservation
     * @param order payment order
     * @return AI_PAY execution request
     */
    public static Execution execution(String id, Order order) {
        return new Execution(IntentActionType.AI_PAY, id, toNode(order));
    }

    /**
     * Decodes the AI_PAY resource whitelist and rejects a missing intent, wrong action or invalid payload.
     * @param intent protocol intent to process
     * @return decoded payment resources
     * @throws A4pException if the required input or payment payload is invalid
     */
    public static ShoppingItems resources(StructuredIntent intent) {
        Validation.present(intent);
        requireAction(intent.action());
        return parse(intent.resources(), ShoppingItems.class);
    }

    /**
     * Decodes the AI_PAY payment constraints and rejects a missing intent, wrong action or invalid payload.
     * @param intent protocol intent to process
     * @return decoded payment constraints
     * @throws A4pException if the required input or payment payload is invalid
     */
    public static PaymentConstraints constraints(StructuredIntent intent) {
        Validation.present(intent);
        requireAction(intent.action());
        return parse(intent.constraints(), PaymentConstraints.class);
    }

    /**
     * Decodes the AI_PAY order and rejects a missing execution, wrong action or invalid payload.
     * @param execution action-specific execution request
     * @return decoded payment order
     * @throws A4pException if the required input or payment payload is invalid
     */
    public static Order order(Execution execution) {
        Validation.present(execution);
        requireAction(execution.action());
        return parse(execution.executionContent(), Order.class);
    }

    /**
     * Returns the current payment accounting snapshot without refreshing its period.
     * @param context activated intent and mutable execution ledger
     * @return quota snapshot without advancing its period
     * @throws A4pException if the required input or payment payload is invalid
     */
    public static PaymentExecutionInfo executionInfo(IntentExecutionContext context) {
        Validation.present(context);
        return context.paymentExecutionInfo();
    }

    /**
     * Requires the supported AI_PAY union discriminator.
     * @param action protocol action selecting the payload schema
     */
    private static void requireAction(IntentActionType action) {
        Validation.require(action == IntentActionType.AI_PAY, A4P_INVALID_PARAMETER);
    }

    /**
     * Converts a payment value to a defensive JSON tree, omitting absent optional fields.
     * @param value value to validate or normalize
     * @return defensive JSON payload, or null for null input
     */
    private static JsonNode toNode(Object value) {
        return value == null ? null : CanonicalJson.freezeNode(PAYLOAD_MAPPER.valueToTree(value));
    }

    // Explicit nulls are rejected like the ArkTS validators do; absent fields are the only way to omit a value.
    /**
     * Decodes a payment payload, rejecting explicit JSON null values and incompatible field types.
     * @param <Value> decoded or copied value type
     * @param node JSON tree to inspect or decode
     * @param type target decoded value type
     * @return decoded or parsed value
     */
    private static <Value> Value parse(JsonNode node, Class<Value> type) {
        Validation.require(node != null && !containsNull(node), A4P_INVALID_PARAMETER);
        try {
            return CanonicalJson.MAPPER.treeToValue(node, type);
        } catch (Exception error) {
            throw new A4pException(A4P_INVALID_PARAMETER);
        }
    }

    /**
     * Checks recursively whether a JSON payload contains an explicit null value.
     * @param node JSON tree to inspect or decode
     * @return true if any node is an explicit JSON null
     */
    private static boolean containsNull(JsonNode node) {
        if (node.isNull()) { return true; }
        for (JsonNode child : node) {
            if (containsNull(child)) { return true; }
        }
        return false;
    }
}
