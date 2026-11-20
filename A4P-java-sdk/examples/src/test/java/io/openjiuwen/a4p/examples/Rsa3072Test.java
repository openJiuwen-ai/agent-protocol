package io.openjiuwen.a4p.examples;

import io.openjiuwen.a4p.A4pErrorCode;
import io.openjiuwen.a4p.A4pException;
import io.openjiuwen.a4p.Models.SignatureInfo;
import org.junit.jupiter.api.Test;
import java.security.KeyPairGenerator;
import java.util.Map;
import static org.junit.jupiter.api.Assertions.*;

class Rsa3072Test {
    @Test void paymentExampleRunsWithOnlyPublicSdkTypes() {
        assertDoesNotThrow(() -> PaymentExample.main(new String[0]));
    }

    @Test void signsAndVerifiesWithoutSdkInternalHelpers() throws Exception {
        KeyPairGenerator generator = KeyPairGenerator.getInstance("RSA");
        generator.initialize(3072);
        var keys = generator.generateKeyPair();
        var signer = Rsa3072.signer("example", keys.getPrivate());
        var verifier = Rsa3072.verifier(Map.of("example", keys.getPublic()));
        byte[] bytes = "example".getBytes(java.nio.charset.StandardCharsets.UTF_8);
        var signature = signer.sign(bytes);
        assertTrue(verifier.verify(bytes, signature).valid());
        assertFalse(verifier.verify("changed".getBytes(java.nio.charset.StandardCharsets.UTF_8), signature).valid());
        assertFalse(verifier.verify(bytes, new SignatureInfo(signature.alg(), signature.keyIdentifier(),
                signature.sign() + "=")).valid());
        assertFalse(verifier.verify(bytes, new SignatureInfo(signature.alg(), "unknown",
                signature.sign())).valid());
        generator.initialize(2048);
        var weak = generator.generateKeyPair();
        assertEquals(A4pErrorCode.A4P_INVALID_PARAMETER,
                assertThrows(A4pException.class, () -> Rsa3072.signer("weak", weak.getPrivate())).code());
    }
}
