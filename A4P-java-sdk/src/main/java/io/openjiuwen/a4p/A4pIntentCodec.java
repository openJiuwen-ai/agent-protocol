package io.openjiuwen.a4p;

import io.openjiuwen.a4p.Models.*;

public interface A4pIntentCodec {
    String serializeIntentL1(IntentL1 intentL1);

    A4pResult<IntentL1> deserializeIntentL1(String serialized);

    String serializeIntentL2(IntentL2 intentL2);

    A4pResult<IntentL2> deserializeIntentL2(String serialized);

    String serializeIntentL3(IntentL3 intentL3);

    A4pResult<IntentL3> deserializeIntentL3(String serialized);

    String serializeIntentExecutionContext(IntentExecutionContext intentExecutionContext);

    A4pResult<IntentExecutionContext> deserializeIntentExecutionContext(String serialized);

    static A4pIntentCodec standard() { return CanonicalIntentCodec.INSTANCE; }
}
