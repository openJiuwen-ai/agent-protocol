package io.openjiuwen.a4p;
import com.fasterxml.jackson.databind.node.ObjectNode;
import io.openjiuwen.a4p.Models.*;
import java.util.List;
import org.junit.jupiter.api.Test;
import static io.openjiuwen.a4p.Fixtures.*;
import static io.openjiuwen.a4p.A4pErrorCode.*;
import static org.junit.jupiter.api.Assertions.*;
class CodecDiagnosticsAdditionalTest {
 private final A4pIntentCodec codec = new A4pIntentCodec();
@org.junit.jupiter.api.Test
    void codecDiagnosticsPreserveReasonsAndCauses() {
        A4pIntentCodec codec = new A4pIntentCodec();
        assertTrue(codec.deserializeIntentL1("").message().contains("must not be null or empty"));
        assertTrue(codec.deserializeIntentL1("{}").message().length() > A4pErrorCode.A4P_DESERIALIZE_FAIL.message().length());
        A4pException error = assertThrows(A4pException.class, () -> codec.serializeIntentL1(null));
        assertEquals(A4pErrorCode.A4P_SERIALIZE_FAIL, error.code());
        assertNotNull(error.getCause());
        assertTrue(error.getMessage().contains("Required value"));
        A4pException parseError = assertThrows(A4pException.class, () -> CanonicalJson.parse("{"));
        assertNotNull(parseError.getCause());
    }
    /** Checks the diagnostic path for a floating-point array element. */
    @Test
    void reportsNestedFloatingPointPathInEveryDecoder() {
        for (String number : List.of("1.5", "1.0", "1e0", "-0.0", "1E+2")) {
            String input = "{\"payload\":{\"items\":[" + number + "]}}";
            for (A4pResult<?> result : List.of(codec.deserializeIntentL1(input),
                    codec.deserializeIntentL2(input), codec.deserializeIntentL3(input),
                    codec.deserializeIntentExecutionContext(input))) {
                fails(A4P_DESERIALIZE_FAIL, result);
                assertTrue(result.message().contains("Floating-point values"));
                assertTrue(result.message().contains("$/payload/items/0"));
            }
        }
    }
}
