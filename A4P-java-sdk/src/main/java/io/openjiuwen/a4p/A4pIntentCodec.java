package io.openjiuwen.a4p;

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
                       JsonNode executionInfo, List<RecordJson> executions, Long activatedTime) {}

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
     * @param serialized serialized local JSON representation
     * @return restored object or a protocol failure result
     */
    public A4pResult<IntentL1> deserializeIntentL1(String serialized) {
        return decode(serialized, IntentL1.class, Validation::intentL1);
    }

    /**
     * Restores IntentL2 from JSON with structural and protocol validation.
     * Does not verify cryptographic signatures or authenticate persisted data.
     * @param serialized serialized local JSON representation
     * @return restored object or a protocol failure result
     */
    public A4pResult<IntentL2> deserializeIntentL2(String serialized) {
        return decode(serialized, IntentL2.class, Validation::intentL2);
    }

    /**
     * Restores IntentL3 from JSON with structural and protocol validation.
     * Does not verify cryptographic signatures or authenticate persisted data.
     * @param serialized serialized local JSON representation
     * @return restored object or a protocol failure result
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
        Validation.present(intentExecutionContext);
        Map<String, Object> fields = new LinkedHashMap<>();
        // One lock for every field, so the ledger and executionInfo come from the same state.
        synchronized (intentExecutionContext) {
            fields.put("version", intentExecutionContext.version());
            fields.put("intentId", intentExecutionContext.intentId());
            fields.put("authorizedIntent", intentExecutionContext.authorizedIntent());
            fields.put("activatedTime", intentExecutionContext.activatedTime());
            fields.put("status", intentExecutionContext.status());
            fields.put("executionInfo", intentExecutionContext.executionInfo());
            fields.put("executions", intentExecutionContext.executions());
        }
        return CanonicalJson.stringify(fields);
    }

    // The user signature is not re-verified; the stored L3 is checked like activateIntent's input otherwise.
    /**
     * Restores IntentExecutionContext from JSON with structural and protocol validation.
     * Does not verify cryptographic signatures or authenticate persisted data.
     * Checks activation time, ledger reachability and accounting consistency; older contexts without activatedTime are rejected.
     * @param serialized serialized local JSON representation
     * @return restored object or a protocol failure result
     */
    public A4pResult<IntentExecutionContext> deserializeIntentExecutionContext(String serialized) {
        return A4pResult.capture(() -> restore(parse(serialized, ContextJson.class)));
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
        Validation.require(intent.intentId().equals(wire.intentId()), A4P_INVALID_PARAMETER);
        Validation.integer(wire.activatedTime(), false);
        Validation.require(wire.activatedTime() >= authorized.timestamp(), A4P_INVALID_PARAMETER);
        Validation.present(wire.status());
        Validation.present(wire.executions());
        List<IntentExecutionContext.Entry> ledger = new ArrayList<>();
        for (RecordJson record : wire.executions()) {
            Validation.present(record);
            Validation.integer(record.reservedAt(), false);
            Validation.require(record.reservedAt() >= wire.activatedTime(), A4P_INVALID_PARAMETER);
            Order order = Validation.execution(new Execution(record.action(), record.executionId(),
                    record.executionContent()));
            ledger.add(new IntentExecutionContext.Entry(new ExecutionRecord(record.executionId(), record.status(),
                    record.action(), record.executionContent(), record.periodIndex(), record.reservedAt(),
                    record.completedAt(), record.canceledAt()), order));
        }
        Long period = period(wire.executionInfo(), PaymentProfile.constraints(intent.structuredIntent()));
        IntentExecutionContext context = new IntentExecutionContext(authorized, wire.status(), ledger, period, wire.activatedTime());
        Validation.ledger(context);
        // executionInfo is derived state, so it must match what the restored ledger yields.
        Validation.require(context.executionInfo().equals(CanonicalJson.freezeNode(wire.executionInfo())),
                A4P_INVALID_PARAMETER);
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
        Validation.require(info != null && info.isObject(), A4P_INVALID_PARAMETER);
        JsonNode period = info.get("periodIndex");
        if (period == null || period.isNull()) {
            Validation.require(constraints.anchorTime() == null, A4P_INVALID_PARAMETER);
            return null;
        }
        Validation.require(constraints.anchorTime() != null && period.isIntegralNumber() && period.canConvertToLong(),
                A4P_INVALID_PARAMETER);
        return period.longValue();
    }

    /**
     * Serializes a non-null protocol object as canonical JSON.
     * @param intent protocol intent to process
     * @return canonical JSON representation
     */
    private static String serialize(Object intent) {
        Validation.present(intent);
        return CanonicalJson.stringify(intent);
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
        return A4pResult.capture(() -> {
            Value value = parse(json, type);
            validate.accept(value);
            return value;
        });
    }

    /**
     * Strictly parses JSON and converts it to the requested protocol representation.
     * @param <Value> decoded or copied value type
     * @param json JSON representation to parse
     * @param type target decoded value type
     * @return decoded or parsed value
     */
    private static <Value> Value parse(String json, Class<Value> type) {
        Validation.require(json != null && !json.isEmpty(), A4P_INVALID_PARAMETER);
        Object parsed = CanonicalJson.parse(json);
        try {
            return CanonicalJson.MAPPER.convertValue(parsed, type);
        } catch (IllegalArgumentException error) {
            throw new A4pException(A4P_INVALID_PARAMETER);
        }
    }
}
