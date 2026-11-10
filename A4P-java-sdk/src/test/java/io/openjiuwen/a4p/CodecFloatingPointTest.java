package io.openjiuwen.a4p;

import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.ValueSource;
import static io.openjiuwen.a4p.Fixtures.*;
import static io.openjiuwen.a4p.A4pErrorCode.*;
import static org.junit.jupiter.api.Assertions.*;

class CodecFloatingPointTest {
    private final A4pIntentCodec codec = new A4pIntentCodec();

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

    @ParameterizedTest
    @ValueSource(strings = {"0.5", "1.0", "1e0", "1e-400"})
    void rejectsDecimalsRecursivelyBeforeTypedMapping(String number) {
        String original = codec.serializeIntentL1(authorized(constraints()).intentL2().intentL1());
        assertTrue(codec.deserializeIntentL1(original).success());
        for (String value : new String[]{number, "{\"nested\":" + number + "}", "[0,{\"nested\":[" + number + "]}]"}) {
            // Assert the codec error specifically: unknown-field mapping alone returns INVALID_PARAMETER.
            assertRejected(0, "{\"probe\":" + value + "," + original.substring(1));
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
