package io.openjiuwen.a4p;

import io.openjiuwen.a4p.Models.*;
import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

class CodecConformanceCheckTest {
    public static class MatchingCodec extends A4pIntentCodec {
        private final A4pIntentCodec delegate = new A4pIntentCodec();
        public String serializeIntentL1(IntentL1 value) { return delegate.serializeIntentL1(value); }
        public A4pResult<IntentL1> deserializeIntentL1(String json) { return delegate.deserializeIntentL1(json); }
        public String serializeIntentL2(IntentL2 value) { return delegate.serializeIntentL2(value); }
        public A4pResult<IntentL2> deserializeIntentL2(String json) { return delegate.deserializeIntentL2(json); }
        public String serializeIntentL3(IntentL3 value) { return delegate.serializeIntentL3(value); }
        public A4pResult<IntentL3> deserializeIntentL3(String json) { return delegate.deserializeIntentL3(json); }
        public String serializeIntentExecutionContext(IntentExecutionContext value) {
            return delegate.serializeIntentExecutionContext(value);
        }
        public A4pResult<IntentExecutionContext> deserializeIntentExecutionContext(String json) {
            return delegate.deserializeIntentExecutionContext(json);
        }
    }

    public static final class DivergentCodec extends MatchingCodec {
        @Override public String serializeIntentL1(IntentL1 value) { return "{}"; }
    }

    @Test void acceptsMatchingCodecAndRejectsDivergentCodec() throws Exception {
        assertDoesNotThrow(() -> CodecConformanceCheck.main(new String[]{MatchingCodec.class.getName()}));
        assertThrows(AssertionError.class,
                () -> CodecConformanceCheck.main(new String[]{DivergentCodec.class.getName()}));
    }
}
