package io.openjiuwen.a4p;
import com.fasterxml.jackson.databind.node.ObjectNode;
import io.openjiuwen.a4p.Models.*;
import java.util.List;
import org.junit.jupiter.api.Test;
import static io.openjiuwen.a4p.Fixtures.*;
import static io.openjiuwen.a4p.A4pErrorCode.*;
import static org.junit.jupiter.api.Assertions.*;
class CodecDecimalAdditionalTest {
 private final A4pIntentCodec codec = new A4pIntentCodec();
    /** Checks the floating-point rejection reason in all four deserializers. */
    @Test
    void rejectsNestedFloatingPointTokensInEveryDecoder() {
        for (String number : List.of("1.5", "1.0", "1e0", "-0.0", "1E+2")) {
            String input = "{\"payload\":{\"items\":[" + number + "]}}";
            for (A4pResult<?> result : List.of(codec.deserializeIntentL1(input),
                    codec.deserializeIntentL2(input), codec.deserializeIntentL3(input),
                    codec.deserializeIntentExecutionContext(input))) {
                assertEquals(A4P_DESERIALIZE_FAIL.code(), result.code());
                assertTrue(result.message().contains("Floating-point values"));
            }
        }
    }
}
