package io.openjiuwen.a4p;

import com.fasterxml.jackson.databind.node.ObjectNode;
import org.junit.jupiter.api.Test;
import static io.openjiuwen.a4p.Fixtures.*;
import static org.junit.jupiter.api.Assertions.*;

class CodecNullTest {
    private final A4pIntentCodec codec = new A4pIntentCodec();

    @Test void optionalNullFieldsAreOmittedAndAllLevelsRoundTrip() {
        var l3 = authorized(constraints());
        var l2 = l3.intentL2();
        var l1 = l2.intentL1();
        assertFalse(codec.serializeIntentL3(l3).contains(":null"));
        assertEquals(l1, ok(codec.deserializeIntentL1(codec.serializeIntentL1(l1))));
        assertEquals(l2, ok(codec.deserializeIntentL2(codec.serializeIntentL2(l2))));
        assertEquals(l3, ok(codec.deserializeIntentL3(codec.serializeIntentL3(l3))));
        var ctx = context();
        String json = codec.serializeIntentExecutionContext(ctx);
        assertFalse(json.contains(":null"));
        assertEquals(json, codec.serializeIntentExecutionContext(ok(codec.deserializeIntentExecutionContext(json))));
    }

    @Test void explicitNullIsRejectedAtEveryLevelAndInNestedContainers() {
        var l3 = authorized(constraints());
        String[] jsons = {codec.serializeIntentL1(l3.intentL2().intentL1()),
            codec.serializeIntentL2(l3.intentL2()), codec.serializeIntentL3(l3),
            codec.serializeIntentExecutionContext(context())};
        for (int i = 0; i < jsons.length; i++) {
            for (String value : new String[]{"null", "{\"nested\":null}", "[null]"}) {
                ObjectNode node = (ObjectNode) CanonicalJson.MAPPER.valueToTree(CanonicalJson.parse(jsons[i]));
                // Unknown fields are ignored by the mapper: rejection must happen before mapping.
                try { node.set("probe", CanonicalJson.MAPPER.readTree(value)); }
                catch (java.io.IOException e) { throw new AssertionError(e); }
                var result = switch (i) {
                    case 0 -> codec.deserializeIntentL1(node.toString());
                    case 1 -> codec.deserializeIntentL2(node.toString());
                    case 2 -> codec.deserializeIntentL3(node.toString());
                    default -> codec.deserializeIntentExecutionContext(node.toString());
                };
                assertFalse(result.success());
            }
        }
    }

    @Test void recursivelyOmitsObjectKeysAndPreservesArrayNulls() {
        var node = CanonicalJson.parse("{\"a\":null,\"b\":[null,{\"c\":null,\"d\":1}]}");
        assertEquals("{\"b\":[null,{\"d\":1}]}", CanonicalJson.stringify(A4pIntentCodec.toJsonNode(node)));
    }
}
