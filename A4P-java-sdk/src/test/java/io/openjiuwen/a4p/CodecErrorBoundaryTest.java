package io.openjiuwen.a4p;

import java.util.List;
import java.util.function.Function;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.function.Executable;
import static io.openjiuwen.a4p.Fixtures.*;
import static org.junit.jupiter.api.Assertions.*;

class CodecErrorBoundaryTest {
    private final A4pIntentCodec codec = new A4pIntentCodec();

    @Test void serializationErrorsPreserveCauseAtAllEntryPoints() {
        for (Executable action : List.<Executable>of(() -> codec.serializeIntentL1(null),
                () -> codec.serializeIntentL2(null), () -> codec.serializeIntentL3(null),
                () -> codec.serializeIntentExecutionContext(null))) {
            var error = assertThrows(A4pException.class, action);
            assertEquals(5001, error.code().code());
            assertNotNull(error.getCause());
            assertTrue(error.getMessage().contains("Required value"));
        }
    }

    /** Checks codec error codes and diagnostic messages for invalid wire input. */
    @Test void decodeErrorsExposeDiagnosticsAcrossAllEntryPoints() {
        for (Function<String, A4pResult<?>> decode : List.<Function<String, A4pResult<?>>>of(
                codec::deserializeIntentL1, codec::deserializeIntentL2,
                codec::deserializeIntentL3, codec::deserializeIntentExecutionContext)) {
            for (String input : new String[]{null, "", "{", "{}", "null", "{\"probe\":1.0}"}) {
                var result = decode.apply(input);
                assertFalse(result.success());
                assertEquals(5002, result.code());
                assertNull(result.data());
                assertTrue(result.message().length() > A4pErrorCode.A4P_DESERIALIZE_FAIL.message().length());
            }
        }
        String json = codec.serializeIntentExecutionContext(context());
        var result = codec.deserializeIntentExecutionContext(json.replace("\"leftAmount\":1000", "\"leftAmount\":\"999\""));
        assertEquals(5002, result.code());
        assertTrue(result.message().contains("leftAmount"));
    }
}
