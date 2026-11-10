package io.openjiuwen.a4p;

import java.util.Map;
import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

class CanonicalJsonRfc8785Test {
    @Test void rfcNumberVectorAndNegativeZero() {
        assertEquals("[333333333.3333333,1e+30,4.5,0.002,1e-27]", CanonicalJson.stringify(
            CanonicalJson.parse("[333333333.33333329,1E30,4.50,2e-3,1e-27]")));
        assertEquals("0", CanonicalJson.stringify(-0.0));
        assertEquals("5e-324", CanonicalJson.stringify(Double.MIN_VALUE));
        assertEquals("1.7976931348623157e+308", CanonicalJson.stringify(Double.MAX_VALUE));
    }

    @Test void sortsKeysByUtf16AndKeepsArrayOrder() {
        assertEquals("{\"\\r\":1,\"1\":2,\"\u0080\":3,\"ö\":4,\"€\":5,\"😀\":6,\"דּ\":7}",
            CanonicalJson.stringify(Map.of("\r",1,"1",2,"\u0080",3,"ö",4,"€",5,"😀",6,"דּ",7)));
        assertEquals("[{\"a\":2,\"z\":1},3,1]", CanonicalJson.stringify(CanonicalJson.parse("[{\"z\":1,\"a\":2},3,1]")));
    }

    @Test void preservesNullAndUnicodeWithoutNormalization() {
        assertEquals("{\"a\":null,\"b\":[null,true,false]}", CanonicalJson.stringify(
            CanonicalJson.parse("{\"b\":[null,true,false],\"a\":null}")));
        assertNotEquals(CanonicalJson.stringify("é"), CanonicalJson.stringify("e\u0301"));
        assertEquals("\"\\u000f\\b\\t\\n\\f\\r/\"", CanonicalJson.stringify("\u000f\b\t\n\f\r/"));
    }

    @Test void rejectsInvalidJsonUnicodeAndNonFiniteNumbers() {
        for (String json : new String[]{"{\"a\":1,\"a\":2}", "{} {}", "\"\\ud800\"", "1e400"}) {
            assertThrows(A4pException.class, () -> CanonicalJson.parse(json));
        }
        assertThrows(A4pException.class, () -> CanonicalJson.stringify(Double.NaN));
        assertThrows(A4pException.class, () -> CanonicalJson.stringify(Double.POSITIVE_INFINITY));
        assertEquals("9007199254740992", CanonicalJson.stringify(CanonicalJson.parse("9007199254740992")));
    }
}
