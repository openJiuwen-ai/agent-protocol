package io.openjiuwen.a4p;
import com.fasterxml.jackson.databind.node.ObjectNode;
import io.openjiuwen.a4p.Models.*;
import java.util.List;
import org.junit.jupiter.api.Test;
import static io.openjiuwen.a4p.Fixtures.*;
import static io.openjiuwen.a4p.A4pErrorCode.*;
import static org.junit.jupiter.api.Assertions.*;
class CanonicalJsonAdditionalTest {
 private final A4pIntentCodec codec = new A4pIntentCodec();
@Test void ecmascriptNumbersAndInvalidUnicode() {
        assertEquals("[333333333.3333333,1e+30,4.5,0.002,1e-27]",
                CanonicalJson.stringify(CanonicalJson.parse(
                        "[333333333.33333329,1E30,4.50,2e-3,1e-27]")));
        assertEquals("0", CanonicalJson.stringify(-0.0));
        assertEquals("9007199254740992", CanonicalJson.stringify(9007199254740992L));
        thrown(A4P_DESERIALIZE_FAIL, () -> CanonicalJson.parse("1e400"));
        thrown(A4P_DESERIALIZE_FAIL, () -> CanonicalJson.parse("\"" + (char) 0xd800 + "\""));
    }
}
