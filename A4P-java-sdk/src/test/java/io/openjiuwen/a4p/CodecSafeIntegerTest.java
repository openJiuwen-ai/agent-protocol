package io.openjiuwen.a4p;

import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.ValueSource;
import static io.openjiuwen.a4p.Fixtures.*;
import static org.junit.jupiter.api.Assertions.*;

class CodecSafeIntegerTest {
    @ParameterizedTest
    @ValueSource(strings = {"9007199254740992", "-9007199254740992", "999999999999999999999999999"})
    void rejectsUnsafeIntegersRecursivelyInEveryDecoder(String number) {
        var codec = new A4pIntentCodec();
        var l3 = authorized(constraints());
        String[] inputs = {codec.serializeIntentL1(l3.intentL2().intentL1()), codec.serializeIntentL2(l3.intentL2()),
            codec.serializeIntentL3(l3), codec.serializeIntentExecutionContext(context())};
        for (String value : new String[]{number, "{\"nested\":[" + number + "]}"}) {
            for (int i = 0; i < inputs.length; i++) {
                String input = "{\"probe\":" + value + "," + inputs[i].substring(1);
                var result = switch(i) {
                    case 0 -> codec.deserializeIntentL1(input);
                    case 1 -> codec.deserializeIntentL2(input);
                    case 2 -> codec.deserializeIntentL3(input);
                    default -> codec.deserializeIntentExecutionContext(input);
                };
                assertEquals(5002, result.code());
            }
        }
    }

    @ParameterizedTest @ValueSource(longs = {0L, 9007199254740991L})
    void acceptsSafeTimestampBoundary(long timestamp) {
        var codec = new A4pIntentCodec();
        String json = codec.serializeIntentL1(authorized(constraints()).intentL2().intentL1());
        assertTrue(codec.deserializeIntentL1(json.replace("\"timestamp\":10000", "\"timestamp\":" + timestamp)).success());
    }
}
