package io.openjiuwen.a4p;

import com.fasterxml.jackson.databind.node.JsonNodeFactory;
import io.openjiuwen.a4p.Models.IntentL1;
import io.openjiuwen.a4p.Models.StructuredIntent;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.ValueSource;
import static io.openjiuwen.a4p.Fixtures.*;
import static io.openjiuwen.a4p.A4pErrorCode.*;
import static org.junit.jupiter.api.Assertions.*;

class CodecFloatingPointTest {
    private final A4pIntentCodec codec = new A4pIntentCodec();

    @Test
    void rejectsNestedFloatingPointValuesDuringSerializationAndSigning() {
        var original = authorized(constraints()).intentL2().intentL1();
        var resources = JsonNodeFactory.instance.objectNode();
        resources.putArray("items").addObject().put("amount", 1.5);
        var structured = new StructuredIntent(original.structuredIntent().action(), resources,
                original.structuredIntent().constraints());
        var intent = new IntentL1(original.version(), original.intentId(), original.intentConversation(),
                original.intentSummary(), structured, original.timestamp(), original.sign());

        var error = assertThrows(A4pException.class, () -> codec.serializeIntentL1(intent));
        assertEquals(A4P_SERIALIZE_FAIL, error.code());
        assertTrue(error.getMessage().contains("$/structuredIntent/resources/items/0/amount"));
        assertNotNull(error.getCause());
        assertEquals(A4P_SERIALIZE_FAIL,
                assertThrows(A4pException.class, () -> A4pIntentCodec.toSignableBytes(intent)).code());
    }

    @ParameterizedTest
    @ValueSource(longs = {9_007_199_254_740_992L, 9_007_199_254_740_993L, -9_007_199_254_740_993L})
    void rejectsUnsafeIntegersBeforeCanonicalization(long timestamp) {
        var original = authorized(constraints()).intentL2().intentL1();
        var intent = new IntentL1(original.version(), original.intentId(), original.intentConversation(),
                original.intentSummary(), original.structuredIntent(), timestamp, original.sign());

        var error = assertThrows(A4pException.class, () -> codec.serializeIntentL1(intent));
        assertEquals(A4P_SERIALIZE_FAIL, error.code());
        assertTrue(error.getMessage().contains("safe range"));
    }

    @Test
    void preservesLargestSafeTimestampAcrossRoundTrip() {
        var original = authorized(constraints()).intentL2().intentL1();
        var intent = new IntentL1(original.version(), original.intentId(), original.intentConversation(),
                original.intentSummary(), original.structuredIntent(), CanonicalJson.MAX_SAFE_INTEGER, original.sign());

        var result = codec.deserializeIntentL1(codec.serializeIntentL1(intent));
        assertTrue(result.success(), result.message());
        assertEquals(intent.timestamp(), result.data().timestamp());
    }

    @ParameterizedTest
    @ValueSource(strings = {"0.5", "-0.5", "1.0", "0.0", "-0.0", "1e0", "1E+3", "1e-3", "1e-400", "9007199254740991.0"})
    void rejectsFloatingPointTokensAcrossAllDecoders(String number) {
        var l3 = authorized(constraints());
        String[] inputs = {codec.serializeIntentL1(l3.intentL2().intentL1()),
            codec.serializeIntentL2(l3.intentL2()), codec.serializeIntentL3(l3),
            codec.serializeIntentExecutionContext(context())};
        for (int i = 0; i < inputs.length; i++) {
            String field = i == 3 ? "activatedTime" : "timestamp";
            String input = inputs[i].replaceFirst("\"" + field + "\":10000", "\"" + field + "\":" + number);
            assertNotEquals(inputs[i], input);
            assertRejected(i, input);
        }
    }

    /**
     * Checks that the diagnostic identifies the floating-point value in the probe field.
     * @param number test input for the numeric field
     */
    @ParameterizedTest
    @ValueSource(strings = {"0.5", "1.0", "1e0", "1e-400"})
    void reportsFloatingPointPathsBeforeRejectingUnknownFields(String number) {
        String original = codec.serializeIntentL1(authorized(constraints()).intentL2().intentL1());
        assertTrue(codec.deserializeIntentL1(original).success());
        for (String value : new String[]{number, "{\"nested\":" + number + "}", "[0,{\"nested\":[" + number + "]}]"}) {
            var result = codec.deserializeIntentL1("{\"probe\":" + value + "," + original.substring(1));
            assertEquals(A4P_DESERIALIZE_FAIL.code(), result.code());
            assertTrue(result.message().contains("Floating-point values are not allowed at $/probe"));
        }
    }

    private void assertRejected(int level, String input) {
        var result = switch (level) {
            case 0 -> codec.deserializeIntentL1(input);
            case 1 -> codec.deserializeIntentL2(input);
            case 2 -> codec.deserializeIntentL3(input);
            default -> codec.deserializeIntentExecutionContext(input);
        };
        assertFalse(result.success());
        assertEquals(A4P_DESERIALIZE_FAIL.code(), result.code());
    }
}
