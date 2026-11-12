package io.openjiuwen.a4p;
import com.fasterxml.jackson.databind.node.ObjectNode;
import io.openjiuwen.a4p.Models.*;
import java.util.List;
import org.junit.jupiter.api.Test;
import static io.openjiuwen.a4p.Fixtures.*;
import static io.openjiuwen.a4p.A4pErrorCode.*;
import static org.junit.jupiter.api.Assertions.*;
class CodecNullAdditionalTest {
 private final A4pIntentCodec codec = new A4pIntentCodec();
    /** Compares codec null omission with canonical JSON serialization. */
    @Test void codecOmitsNullObjectFieldsAndCanonicalJsonPreservesThem() {
 String input = "{\"z\":null,\"a\":[null,{\"b\":null}]}";
 assertEquals("{\"a\":[null,{\"b\":null}],\"z\":null}", CanonicalJson.stringify(CanonicalJson.parse(input)));
 assertEquals("{\"a\":[null,{}]}", CanonicalJson.stringify(A4pIntentCodec.toJsonNode(CanonicalJson.parse(input))));
 assertEquals("null", CanonicalJson.stringify(CanonicalJson.parse("null")));
}
@Test void rejectsNullInKnownOptionalField() {
 String json = codec.serializeIntentL3(authorized(constraints()));
 assertTrue(codec.deserializeIntentL3(json).success());
 String changed = json.replace("\"singleOrderMaxAmount\":500", "\"singleOrderMaxAmount\":null");
 assertNotEquals(json, changed);
 assertFalse(codec.deserializeIntentL3(changed).success());
}

}
