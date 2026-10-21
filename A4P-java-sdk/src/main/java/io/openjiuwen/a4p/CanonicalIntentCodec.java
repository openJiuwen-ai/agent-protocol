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
final class CanonicalIntentCodec implements A4pIntentCodec {
    static final CanonicalIntentCodec INSTANCE = new CanonicalIntentCodec();

    // Wire form of a persisted context; reservedAt is boxed so a missing field is rejected instead of read as 0.
    record ContextJson(String version, String intentId, IntentL3 authorizedIntent, IntentExecutionStatus status,
                       JsonNode executionInfo, List<RecordJson> executions) {}

    record RecordJson(String executionId, ExecutionStatus status, Long periodIndex, Long reservedAt,
                      Long completedAt, Long canceledAt, Execution execution) {}

    private CanonicalIntentCodec() {}

    @Override
    public String serializeIntentL1(IntentL1 intentL1) { return serialize(intentL1); }

    @Override
    public String serializeIntentL2(IntentL2 intentL2) { return serialize(intentL2); }

    @Override
    public String serializeIntentL3(IntentL3 intentL3) { return serialize(intentL3); }

    @Override
    public A4pResult<IntentL1> deserializeIntentL1(String serialized) {
        return decode(serialized, IntentL1.class, Validation::intentL1);
    }

    @Override
    public A4pResult<IntentL2> deserializeIntentL2(String serialized) {
        return decode(serialized, IntentL2.class, Validation::intentL2);
    }

    @Override
    public A4pResult<IntentL3> deserializeIntentL3(String serialized) {
        return decode(serialized, IntentL3.class, Validation::intentL3);
    }

    @Override
    public String serializeIntentExecutionContext(IntentExecutionContext intentExecutionContext) {
        Validation.present(intentExecutionContext);
        Map<String, Object> fields = new LinkedHashMap<>();
        // One lock for every field, so the ledger and executionInfo come from the same state.
        synchronized (intentExecutionContext) {
            fields.put("version", intentExecutionContext.version());
            fields.put("intentId", intentExecutionContext.intentId());
            fields.put("authorizedIntent", intentExecutionContext.authorizedIntent());
            fields.put("status", intentExecutionContext.status());
            fields.put("executionInfo", intentExecutionContext.executionInfo());
            fields.put("executions", intentExecutionContext.executions());
        }
        return CanonicalJson.stringify(fields);
    }

    // The user signature is not re-verified; the stored L3 is checked like activateIntent's input otherwise.
    @Override
    public A4pResult<IntentExecutionContext> deserializeIntentExecutionContext(String serialized) {
        return A4pResult.capture(() -> restore(parse(serialized, ContextJson.class)));
    }

    private static IntentExecutionContext restore(ContextJson wire) {
        Validation.present(wire);
        Validation.text(wire.version());
        Validation.version(wire.version());
        IntentL3 authorized = wire.authorizedIntent();
        Validation.intentL3(authorized);
        Validation.versions(authorized);
        IntentL1 intent = authorized.intentL2().intentL1();
        Validation.require(intent.intentId().equals(wire.intentId()), A4P_INVALID_PARAMETER);
        Validation.present(wire.status());
        Validation.present(wire.executions());
        List<IntentExecutionContext.Entry> ledger = new ArrayList<>();
        for (RecordJson record : wire.executions()) {
            Validation.present(record);
            Validation.integer(record.reservedAt(), false);
            Order order = Validation.execution(record.execution());
            ledger.add(new IntentExecutionContext.Entry(new ExecutionRecord(record.executionId(), record.status(),
                    record.periodIndex(), record.reservedAt(), record.completedAt(), record.canceledAt(),
                    record.execution()), order));
        }
        Long period = period(wire.executionInfo(), PaymentProfile.constraints(intent.structuredIntent()));
        IntentExecutionContext context = new IntentExecutionContext(authorized, wire.status(), ledger, period);
        Validation.ledger(context);
        // executionInfo is derived state, so it must match what the restored ledger yields.
        Validation.require(context.executionInfo().equals(CanonicalJson.freezeNode(wire.executionInfo())),
                A4P_INVALID_PARAMETER);
        return context;
    }

    // The period executionInfo was last recomputed in depends on call times the ledger does not record.
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

    private static String serialize(Object intent) {
        Validation.present(intent);
        return CanonicalJson.stringify(intent);
    }

    private static <Value> A4pResult<Value> decode(String json, Class<Value> type, Consumer<Value> validate) {
        return A4pResult.capture(() -> {
            Value value = parse(json, type);
            validate.accept(value);
            return value;
        });
    }

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
