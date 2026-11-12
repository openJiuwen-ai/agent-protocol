package io.openjiuwen.a4p;

import java.util.function.Supplier;

import com.fasterxml.jackson.databind.JsonNode;
import io.openjiuwen.a4p.Models.*;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.function.Consumer;
import static io.openjiuwen.a4p.A4pErrorCode.*;

// Serializes to canonical JSON; deserialization parses strictly and validates the whole chain.
/**
 * Provides canonical JSON serialization and structural restoration checks.
 * Restoration validates layered intents, ledger reachability and accounting consistency,
 * but does not verify signatures or provide integrity protection for persisted data.
 */
public class A4pIntentCodec {

    // Wire form of a persisted context; reservedAt is boxed so a missing field is rejected instead of read as 0.
    /**
     * Represents persisted context fields; restoration checks consistency without authenticating the data.
     * @param version A4P Core protocol version
     * @param intentId globally unique intent identifier
     * @param authorizedIntent authorized L3 snapshot
     * @param status context execution state
     * @param executionInfo persisted quota snapshot
     * @param executions persisted execution ledger
     * @param activatedTime activation instant in Unix epoch milliseconds
     */
    record ContextJson(String version, String intentId, IntentL3 authorizedIntent, IntentExecutionStatus status,
                       JsonNode executionInfo, List<RecordJson> executions, Long activatedTime, Long revokedTime) {}

    /**
     * Represents a persisted execution record; boxed reservedAt allows missing timestamps to be rejected.
     * @param executionId execution identifier, retained even after cancellation
     * @param status execution record state
     * @param action protocol action selecting the payload schema
     * @param executionContent action-specific JSON payload; AI_PAY contains an Order
     * @param periodIndex original reservation period, or null without periodic limits
     * @param reservedAt reservation instant in Unix epoch milliseconds
     * @param completedAt completion instant, present only for COMPLETED and not before reservedAt
     * @param canceledAt cancellation instant, present only for CANCELED and not before reservedAt
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
     * Restores IntentL1 from JSON with structural and protocol validation.
     * Does not verify cryptographic signatures or authenticate persisted data.
     * @param serialized JSON exactly matching the corresponding serializer output
     * @return restored object, code 5002 for all decoding and restoration failures
     */
    public A4pResult<IntentL1> deserializeIntentL1(String serialized) {
        return decode(serialized, IntentL1.class, Validation::intentL1);
    }

    /**
     * Restores IntentL2 from JSON with structural and protocol validation.
     * Does not verify cryptographic signatures or authenticate persisted data.
     * @param serialized JSON exactly matching the corresponding serializer output
     * @return restored object, code 5002 for all decoding and restoration failures
     */
    public A4pResult<IntentL2> deserializeIntentL2(String serialized) {
        return decode(serialized, IntentL2.class, Validation::intentL2);
    }

    /**
     * Restores IntentL3 from JSON with structural and protocol validation.
     * Does not verify cryptographic signatures or authenticate persisted data.
     * @param serialized JSON exactly matching the corresponding serializer output
     * @return restored object, code 5002 for all decoding and restoration failures
     */
    public A4pResult<IntentL3> deserializeIntentL3(String serialized) {
        return decode(serialized, IntentL3.class, Validation::intentL3);
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

    // The user signature is not re-verified; the stored L3 is checked like activateIntent's input otherwise.
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
     * Restores a context after checking activation time, layered intent structure, ledger constraints and accounting consistency.
     * Does not reverify user signatures or authenticate persistence.
     * @param wire parsed persistence representation
     * @return structurally validated execution context
     */
    private static IntentExecutionContext restore(ContextJson wire) {
        Validation.present(wire);
        Validation.text(wire.version());
        Validation.version(wire.version());
        IntentL3 authorized = wire.authorizedIntent();
        Validation.intentL3(authorized);
        Validation.versions(authorized);
        IntentL1 intent = authorized.intentL2().intentL1();
        Validation.require(intent.intentId().equals(wire.intentId()), A4P_INVALID_PARAMETER, "Context intent ID does not match the authorized intent ID");
        Validation.integer(wire.activatedTime(), false);
        Validation.present(wire.status());
        Validation.present(wire.executions());
        List<IntentExecutionContext.Entry> ledger = new ArrayList<>();
        for (RecordJson record : wire.executions()) {
            Validation.present(record);
            Validation.integer(record.reservedAt(), false);
            Order order = Validation.execution(new Execution(record.action(), record.executionId(),
                    record.executionContent()));
            ledger.add(new IntentExecutionContext.Entry(new ExecutionRecord(record.executionId(), record.status(),
                    record.action(), record.executionContent(), record.periodIndex(), record.reservedAt(),
                    record.completedAt(), record.canceledAt()), order));
        }
        Long period = period(wire.executionInfo(), PaymentProfile.constraints(intent.structuredIntent()));
        IntentExecutionContext context = new IntentExecutionContext(authorized, wire.status(), ledger, period, wire.activatedTime(), wire.revokedTime());
        if (wire.status() == IntentExecutionStatus.REVOKED) {
            Validation.integer(wire.revokedTime(), false);
        } else {
            Validation.require(wire.revokedTime() == null, A4P_INVALID_PARAMETER, "Revocation time must be absent unless the intent is revoked");
        }
        Validation.ledger(context);
        // executionInfo is derived state, so it must match what the restored ledger yields.
        Validation.require(context.executionInfo().equals(CanonicalJson.freezeNode(wire.executionInfo())), A4P_INVALID_PARAMETER, "Serialized execution info does not match the restored ledger");
        return context;
    }

    // The period executionInfo was last recomputed in depends on call times the ledger does not record.
    /**
     * Reads the stored accounting period and checks that it agrees with the configured periodic policy.
     * @param info quota accounting snapshot
     * @param constraints authorized AI_PAY payment limits
     * @return computed or stored accounting period, or null without periodic limits
     */
    private static Long period(JsonNode info, PaymentConstraints constraints) {
        Validation.require(info != null && info.isObject(), A4P_INVALID_PARAMETER, "Execution info must be a non-null JSON object");
        JsonNode period = info.get("periodIndex");
        if (period == null || period.isNull()) {
            Validation.require(constraints.anchorTime() == null, A4P_INVALID_PARAMETER, "Periodic intent requires a period index");
            return null;
        }
        Validation.require(constraints.anchorTime() != null && period.isIntegralNumber() && period.canConvertToLong(), A4P_INVALID_PARAMETER, "Period index requires periodic constraints and must be a long integer");
        return period.longValue();
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
            return CanonicalJson.MAPPER.convertValue(parsed, type);
        } catch (IllegalArgumentException error) {
            throw new A4pException(A4P_INVALID_PARAMETER, "JSON object mapping failed: " + error.getMessage(), error);
        }
    }
}
