package io.openjiuwen.a4p;

import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.ValueSource;
import static io.openjiuwen.a4p.Fixtures.*;
import static org.junit.jupiter.api.Assertions.*;

class CodecSafeIntegerTest {
    /**
     * Checks rejection outside the interoperable safe integer range.
     * @param timestamp test input for the numeric field
     */
    @ParameterizedTest
    @ValueSource(strings = {"9007199254740992", "-9007199254740992", "9223372036854775808", "-9223372036854775809"})
    void rejectsTimestampsOutsideSafeIntegerRange(String timestamp) {
        var codec = new A4pIntentCodec();
        String json = codec.serializeIntentL1(authorized(constraints()).intentL2().intentL1());
        assertEquals(5002, codec.deserializeIntentL1(
                json.replace("\"timestamp\":10000", "\"timestamp\":" + timestamp)).code());
    }

    /**
     * Checks that deserialization preserves timestamps at both safe integer endpoints.
     * @param timestamp test input for the numeric field
     */
    @ParameterizedTest @ValueSource(longs = {-9007199254740991L, 0L, 9007199254740991L})
    void acceptsSafeIntegerTimestamps(long timestamp) {
        var codec = new A4pIntentCodec();
        String json = codec.serializeIntentL1(authorized(constraints()).intentL2().intentL1());
        assertEquals(timestamp, ok(codec.deserializeIntentL1(
                json.replace("\"timestamp\":10000", "\"timestamp\":" + timestamp))).timestamp());
    }
}
