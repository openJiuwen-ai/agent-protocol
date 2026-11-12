package io.openjiuwen.a4p;

import java.util.function.Supplier;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.cfg.CoercionAction;
import com.fasterxml.jackson.databind.cfg.CoercionInputShape;
import com.fasterxml.jackson.databind.json.JsonMapper;
import com.fasterxml.jackson.databind.type.LogicalType;
import io.openjiuwen.a4p.Models.*;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.function.Consumer;
import static io.openjiuwen.a4p.A4pErrorCode.*;

/** Serializes canonical JSON and restores objects with required-field and type checks. */
public class A4pIntentCodec {
    private static final JsonMapper WIRE_MAPPER = CanonicalJson.MAPPER.rebuild()
            .withCoercionConfig(LogicalType.Textual, config -> config
                    .setCoercion(CoercionInputShape.Integer, CoercionAction.Fail)
                    .setCoercion(CoercionInputShape.Float, CoercionAction.Fail)
                    .setCoercion(CoercionInputShape.Boolean, CoercionAction.Fail))
            .build();

    /**
     * Represents the persisted quota snapshot with boxed values for presence checks.
     * @param leftAmount required remaining amount
     * @param leftExecutionCount optional remaining total execution count
     * @param periodIndex optional accounting period
     * @param leftPeriodExecutionCount optional remaining count for that period
     */
    record QuotaJson(Long leftAmount, Long leftExecutionCount, Long periodIndex,
                     Long leftPeriodExecutionCount) {}

    // Wire form of a persisted context; reservedAt is boxed so a missing field is rejected instead of read as 0.
    /**
     * Represents persisted context fields; restoration checks required fields and types.
     * @param version A4P Core protocol version
     * @param intentId globally unique intent identifier
     * @param authorizedIntent authorized L3 snapshot
     * @param status context execution state
     * @param executionInfo persisted quota snapshot
     * @param executions persisted execution ledger
     * @param activatedTime activation instant in Unix epoch milliseconds
     * @param revokedTime optional revocation instant
     */
    record ContextJson(String version, String intentId, IntentL3 authorizedIntent, IntentExecutionStatus status,
                       QuotaJson executionInfo, List<RecordJson> executions, Long activatedTime, Long revokedTime) {}

    /**
     * Represents a persisted execution record; boxed reservedAt allows missing timestamps to be rejected.
     * @param executionId execution identifier, retained even after cancellation
     * @param status execution record state
     * @param action protocol action selecting the payload schema
     * @param executionContent action-specific JSON payload; AI_PAY contains an Order
     * @param periodIndex original reservation period, or null without periodic limits
     * @param reservedAt reservation instant in Unix epoch milliseconds
     * @param completedAt completion instant, required for COMPLETED
     * @param canceledAt cancellation instant, required for CANCELED
     */
    record RecordJson(String executionId, ExecutionStatus status, IntentActionType action,
                      JsonNode executionContent, Long periodIndex, Long reservedAt,
                      Long completedAt, Long canceledAt) {}

    /**
     * Constructs a reusable canonical intent codec.
     */
    public A4pIntentCodec() {}

    /**
     * Serializes the SDK-created IntentL1 as canonical JSON.
     * @param intentL1 SDK-created first-layer intent
     * @return canonical JSON representation
     * @throws A4pException if the input cannot be serialized
     */
    public String serializeIntentL1(IntentL1 intentL1) { return serialize(intentL1); }

    /**
     * Serializes the SDK-created IntentL2 as canonical JSON.
     * @param intentL2 SDK-issued second-layer intent
     * @return canonical JSON representation
     * @throws A4pException if the input cannot be serialized
     */
    public String serializeIntentL2(IntentL2 intentL2) { return serialize(intentL2); }

    /**
     * Serializes the SDK-created IntentL3 as canonical JSON.
     * @param intentL3 SDK-authorized third-layer intent
     * @return canonical JSON representation
     * @throws A4pException if the input cannot be serialized
     */
    public String serializeIntentL3(IntentL3 intentL3) { return serialize(intentL3); }

    /**
     * Restores IntentL1 from JSON with required-field and type checks.
     * Does not verify cryptographic signatures or authenticate persisted data.
     * @param serialized JSON exactly matching the corresponding serializer output
     * @return restored object, code 5002 for all decoding and restoration failures
     */
    public A4pResult<IntentL1> deserializeIntentL1(String serialized) {
        return decode(serialized, IntentL1.class, A4pIntentCodec::requireL1Fields);
    }

    /**
     * Restores IntentL2 from JSON with required-field and type checks.
     * Does not verify cryptographic signatures or authenticate persisted data.
     * @param serialized JSON exactly matching the corresponding serializer output
     * @return restored object, code 5002 for all decoding and restoration failures
     */
    public A4pResult<IntentL2> deserializeIntentL2(String serialized) {
        return decode(serialized, IntentL2.class, A4pIntentCodec::requireL2Fields);
    }

    /**
     * Restores IntentL3 from JSON with required-field and type checks.
     * Does not verify cryptographic signatures or authenticate persisted data.
     * @param serialized JSON exactly matching the corresponding serializer output
     * @return restored object, code 5002 for all decoding and restoration failures
     */
    public A4pResult<IntentL3> deserializeIntentL3(String serialized) {
        return decode(serialized, IntentL3.class, A4pIntentCodec::requireL3Fields);
    }

    /**
     * Serializes the SDK-created IntentExecutionContext as canonical JSON.
     * Context output is for local persistence only; callers provide storage integrity protection.
     * @param intentExecutionContext context for local persistence only
     * @return canonical JSON representation
     * @throws A4pException if the input cannot be serialized
     */
    public String serializeIntentExecutionContext(IntentExecutionContext intentExecutionContext) {
        return serializeSafely(() -> {
            Validation.present(intentExecutionContext);
            Map<String, Object> fields = new LinkedHashMap<>();
            // One lock for every field, so the ledger and executionInfo come from the same state.
            synchronized (intentExecutionContext) {
                fields.put("version", intentExecutionContext.version());
                fields.put("intentId", intentExecutionContext.intentId());
                fields.put("authorizedIntent", intentExecutionContext.authorizedIntent());
                fields.put("activatedTime", intentExecutionContext.activatedTime());
                if (intentExecutionContext.revokedTime() != null) {
                    fields.put("revokedTime", intentExecutionContext.revokedTime());
                }
                fields.put("status", intentExecutionContext.status());
                fields.put("executionInfo", intentExecutionContext.executionInfo());
                fields.put("executions", intentExecutionContext.executions());
            }
            return CanonicalJson.stringify(toJsonNode(fields));
        });
    }

    /**
     * Restores IntentExecutionContext from JSON with structural and protocol validation.
     * Does not verify cryptographic signatures or authenticate persisted data.
     * Checks integer timestamp formats, ledger reachability and accounting consistency; older contexts without activatedTime are rejected.
     * Audit timestamps are not compared with each other, execution times or authorization validity boundaries.
     * @param serialized JSON exactly matching the corresponding serializer output
     * @return restored object, code 5002 for all decoding and restoration failures
     */
    public A4pResult<IntentExecutionContext> deserializeIntentExecutionContext(String serialized) {
        return deserializeSafely(() -> {
            IntentExecutionContext context = restore(parse(serialized, ContextJson.class));
            requireSerializedForm(serialized, serializeIntentExecutionContext(context));
            return context;
        });
    }

    /**
     * Checks required context and ledger fields and restores the persisted quota snapshot.
     * @param wire decoded context fields
     * @return restored execution context
     */
    private static IntentExecutionContext restore(ContextJson wire) {
        requireFields(wire.version(), wire.intentId(), wire.status(), wire.activatedTime(),
                wire.executionInfo(), wire.executions());
        requireL3Fields(wire.authorizedIntent());
        QuotaJson quota = wire.executionInfo();
        requireFields(quota.leftAmount());
        List<IntentExecutionContext.Entry> ledger = new ArrayList<>();
        for (RecordJson record : wire.executions()) {
            requireFields(record.executionId(), record.status(), record.action(), record.executionContent(),
                    record.reservedAt());
            if (record.status() == ExecutionStatus.COMPLETED) {
                requireFields(record.completedAt());
            }
            if (record.status() == ExecutionStatus.CANCELED) {
                requireFields(record.canceledAt());
            }
            Order order = WIRE_MAPPER.convertValue(record.executionContent(), Order.class);
            requireFields(order.orderId(), order.amount(), order.currency(), order.cartItems(), order.timestamp());
            for (CartItem item : order.cartItems()) {
                requireFields(item.quantity());
                requireProductFields(item.product());
            }
            ledger.add(new IntentExecutionContext.Entry(new ExecutionRecord(record.executionId(), record.status(),
                    record.action(), record.executionContent(), record.periodIndex(), record.reservedAt(),
                    record.completedAt(), record.canceledAt()), order));
        }
        PaymentExecutionInfo info = new PaymentExecutionInfo(quota.leftExecutionCount(), quota.leftAmount(),
                quota.periodIndex(), quota.leftPeriodExecutionCount());
        return new IntentExecutionContext(wire.version(), wire.intentId(), wire.authorizedIntent(),
                wire.status(), ledger, info, wire.activatedTime(), wire.revokedTime());
    }

    /**
     * Checks required L1 fields and maps the AI_PAY resources and constraints to their wire types.
     * @param intent first-layer intent to check
     */
    private static void requireL1Fields(IntentL1 intent) {
        requireFields(intent);
        requireFields(intent.version(), intent.intentId(), intent.intentConversation(), intent.intentSummary(),
                intent.timestamp(), intent.structuredIntent());
        requireSignatureFields(intent.sign());
        StructuredIntent structured = intent.structuredIntent();
        requireFields(structured.action(), structured.resources(), structured.constraints());
        ShoppingItems resources = WIRE_MAPPER.convertValue(structured.resources(), ShoppingItems.class);
        requireFields(resources.itemsWhiteList());
        for (ItemWhiteList group : resources.itemsWhiteList()) {
            requireFields(group.whiteList(), group.quantity());
            group.whiteList().forEach(A4pIntentCodec::requireProductFields);
        }
        PaymentConstraints constraints = WIRE_MAPPER.convertValue(structured.constraints(), PaymentConstraints.class);
        requireFields(constraints.totalMaxAmount(), constraints.currency(), constraints.validFrom(),
                constraints.validUntil());
    }

    /**
     * Checks required L2 fields, including the identities and embedded L1.
     * @param intent second-layer intent to check
     */
    private static void requireL2Fields(IntentL2 intent) {
        requireFields(intent);
        requireFields(intent.version(), intent.timestamp(), intent.userIdentity(), intent.agentIdentity(),
                intent.serviceProviderIdentity());
        requireFields(intent.userIdentity().identityProvider(), intent.userIdentity().id(),
                intent.agentIdentity().identityProvider(), intent.agentIdentity().id(),
                intent.serviceProviderIdentity().id());
        requireSignatureFields(intent.sign());
        requireL1Fields(intent.intentL1());
    }

    /**
     * Checks required L3 fields, including the signing environment and embedded L2.
     * @param intent third-layer intent to check
     */
    private static void requireL3Fields(IntentL3 intent) {
        requireFields(intent);
        requireFields(intent.version(), intent.timestamp(), intent.signingEnv());
        requireFields(intent.signingEnv().device(), intent.signingEnv().app());
        requireSignatureFields(intent.sign());
        requireL2Fields(intent.intentL2());
    }

    /**
     * Checks that all signature metadata fields are present.
     * @param signature decoded signature metadata
     */
    private static void requireSignatureFields(SignatureInfo signature) {
        requireFields(signature);
        requireFields(signature.alg(), signature.keyIdentifier(), signature.sign());
    }

    /**
     * Checks the seller ID and requires at least one SPU or SKU identifier.
     * @param product decoded payment product
     */
    private static void requireProductFields(Product product) {
        requireFields(product);
        requireFields(product.seller());
        requireFields(product.seller().id(), product.spusku());
        Validation.require(product.spusku().spuId() != null || product.spusku().skuId() != null,
                A4P_INVALID_PARAMETER, "Product must specify at least one SPU or SKU ID");
    }

    /**
     * Requires every supplied field to be non-null.
     * @param values decoded fields that are required
     */
    private static void requireFields(Object... values) {
        for (Object value : values) {
            Validation.present(value);
        }
    }

    /**
     * Serializes a non-null protocol object as canonical JSON.
     * @param intent protocol intent to process
     * @return canonical JSON representation
     */
    private static String serialize(Object intent) {
        return serializeSafely(() -> {
            Validation.present(intent);
            return CanonicalJson.stringify(toJsonNode(intent));
        });
    }

    /** Maps all serialization failures to the codec error while preserving the diagnostic cause. */
    private static String serializeSafely(Supplier<String> action) {
        try {
            return action.get();
        } catch (RuntimeException error) {
            throw new A4pException(A4P_SERIALIZE_FAIL, A4P_SERIALIZE_FAIL.message() + ": " + error.getMessage(), error);
        }
    }

    /** Maps every decoding and restoration failure to 5002 with diagnostics. */
    private static <T> A4pResult<T> deserializeSafely(Supplier<T> action) {
        try {
            return A4pResult.ok(action.get());
        } catch (RuntimeException error) {
            return A4pResult.failure(A4P_DESERIALIZE_FAIL,
                    A4P_DESERIALIZE_FAIL.message() + ": " + error.getMessage());
        }
    }

    /** Removes only the outer signature after applying protocol wire mapping. */
    static byte[] toSignableBytes(Object intent) {
        JsonNode node = toJsonNode(intent);
        if (!node.isObject()) { throw new A4pException(A4P_SERIALIZE_FAIL); }
        ((com.fasterxml.jackson.databind.node.ObjectNode) node).remove("sign");
        return CanonicalJson.encodeUtf8(CanonicalJson.stringify(node));
    }

    /** Converts protocol values to JSON, omitting null object fields and validating numbers recursively. */
    static JsonNode toJsonNode(Object value) {
        JsonNode node = omitNullFields(CanonicalJson.MAPPER.valueToTree(value));
        validateNumbers(node, A4P_SERIALIZE_FAIL);
        return node;
    }

    static JsonNode omitNullFields(JsonNode node) {
        if (node.isObject()) {
            var result = CanonicalJson.MAPPER.createObjectNode();
            node.properties().forEach(entry -> {
                if (!entry.getValue().isNull()) {
                    result.set(entry.getKey(), omitNullFields(entry.getValue()));
                }
            });
            return result;
        }
        if (node.isArray()) {
            var result = CanonicalJson.MAPPER.createArrayNode();
            node.forEach(item -> result.add(omitNullFields(item)));
            return result;
        }
        return node.deepCopy();
    }

    /**
     * Parses a typed protocol value and captures structural validation failures as results.
     * @param <Value> decoded or copied value type
     * @param json JSON representation to parse
     * @param type target decoded value type
     * @param validate structural validation action
     * @return validated value or a protocol failure
     */
    private static <Value> A4pResult<Value> decode(String json, Class<Value> type, Consumer<Value> validate) {
        return deserializeSafely(() -> {
            Value value = parse(json, type);
            validate.accept(value);
            requireSerializedForm(json, serialize(value));
            return value;
        });
    }

    /** Requires the original text to match the serializer output exactly. */
    private static void requireSerializedForm(String input, String serialized) {
        Validation.require(input.equals(serialized), A4P_DESERIALIZE_FAIL,
                "JSON input must exactly match the canonical serialization");
    }

    /** Rejects explicit nulls recursively, including array elements, before typed conversion. */
    private static void rejectNullValues(JsonNode node) {
        Validation.require(node != null && !node.isNull(), A4P_INVALID_PARAMETER,
                "JSON values must not be null; omit absent object fields");
        if (node.isContainerNode()) {
            node.forEach(A4pIntentCodec::rejectNullValues);
        }
    }

    /** Applies the protocol numeric constraints before canonicalization or typed conversion. */
    private static void validateNumbers(JsonNode node, A4pErrorCode code) {
        rejectFloatingPointValues(node, "$", code);
        rejectUnsafeIntegers(node, code);
    }

    /** Rejects integers outside the protocol's safe range recursively. */
    private static void rejectUnsafeIntegers(JsonNode node, A4pErrorCode code) {
        if (node.isIntegralNumber()) {
            Validation.require(node.bigIntegerValue().abs().compareTo(
                    java.math.BigInteger.valueOf(CanonicalJson.MAX_SAFE_INTEGER)) <= 0,
                    code, "Integer exceeds the interoperable safe range");
        }
        if (node.isContainerNode()) {
            node.forEach(child -> rejectUnsafeIntegers(child, code));
        }
    }

    /** Rejects floating-point nodes recursively and reports their location. */
    private static void rejectFloatingPointValues(JsonNode node, String path, A4pErrorCode code) {
        Validation.require(!node.isFloatingPointNumber(), code,
                "Floating-point values are not allowed at " + path);
        if (node.isObject()) {
            node.properties().forEach(entry -> rejectFloatingPointValues(entry.getValue(),
                    path + "/" + entry.getKey().replace("~", "~0").replace("/", "~1"), code));
        } else if (node.isArray()) {
            for (int index = 0; index < node.size(); index++) {
                rejectFloatingPointValues(node.get(index), path + "/" + index, code);
            }
        }
    }

    /**
     * Strictly parses JSON and converts it to the requested protocol representation.
     * @param <Value> decoded or copied value type
     * @param json JSON representation to parse
     * @param type target decoded value type
     * @return decoded or parsed value
     */
    private static <Value> Value parse(String json, Class<Value> type) {
        Validation.require(json != null && !json.isEmpty(), A4P_INVALID_PARAMETER, "JSON input must not be null or empty");
        JsonNode parsed = CanonicalJson.MAPPER.valueToTree(CanonicalJson.parse(json));
        rejectNullValues(parsed);
        validateNumbers(parsed, A4P_DESERIALIZE_FAIL);
        try {
            return WIRE_MAPPER.convertValue(parsed, type);
        } catch (IllegalArgumentException error) {
            throw new A4pException(A4P_INVALID_PARAMETER, "JSON object mapping failed: " + error.getMessage(), error);
        }
    }
}
