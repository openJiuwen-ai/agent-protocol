package io.openjiuwen.a4p;

import io.openjiuwen.a4p.Models.*;
import java.security.KeyPairGenerator;
import java.security.PublicKey;
import java.util.HashMap;
import java.util.Map;
import org.junit.jupiter.api.Test;
import static io.openjiuwen.a4p.Fixtures.*;
import static org.junit.jupiter.api.Assertions.*;

class Rsa3072Test {
    @Test
    void realSignaturesSurviveCodecAndRejectTamperingAndAlgorithmConfusion() throws Exception {
        KeyPairGenerator generator = KeyPairGenerator.getInstance("RSA");
        generator.initialize(3072);
        var agent = generator.generateKeyPair();
        var server = generator.generateKeyPair();
        var user = generator.generateKeyPair();
        Signer agentSigner = Rsa3072.signer("agent", agent.getPrivate());
        Signer serverSigner = Rsa3072.signer("server", server.getPrivate());
        Signer userSigner = Rsa3072.signer("user", user.getPrivate());
        SignatureVerifier agentVerifier = Rsa3072.verifier(Map.of("agent", agent.getPublic()));
        SignatureVerifier serverVerifier = Rsa3072.verifier(Map.of("server", server.getPublic()));
        SignatureVerifier userVerifier = Rsa3072.verifier(Map.of("user", user.getPublic()));
        IntentL1 first = ok(A4p.createIntent(request(constraints()), agentSigner, NOW));
        IntentL2 second = ok(A4p.issueIntent(first, agentVerifier, issueContext(), serverSigner, NOW));
        IntentL3 third = ok(A4p.signIntent(second, serverVerifier, new SigningEnv(Map.of(), Map.of()), userSigner, NOW));
        A4pIntentCodec codec = A4pIntentCodec.standard();
        IntentL3 decoded = ok(codec.deserializeIntentL3(codec.serializeIntentL3(third)));
        assertEquals(IntentExecutionStatus.ACTIVE, ok(A4p.activateIntent(decoded, userVerifier, NOW)).status());

        // verifyIntentAuthenticity checks the intent center (L2) signature, not the user's.
        VerificationResult issued = ok(A4p.verifyIntentAuthenticity(decoded, serverVerifier));
        assertTrue(issued.valid());
        assertEquals(A4pErrorCode.A4P_SUCCESS.code(), issued.code());
        assertFalse(ok(A4p.verifyIntentAuthenticity(decoded, userVerifier)).valid());

        IntentL3 tampered = new IntentL3(third.version(), third.intentL2(), third.signingEnv(), NOW + 1, third.sign());
        A4pErrorCode verifyFail = A4pErrorCode.A4P_VERIFY_FAIL;
        fails(verifyFail, A4p.activateIntent(tampered, userVerifier, NOW + 1));
        byte[] bytes = CanonicalJson.toSignableBytes(third);
        VerificationResult confused = userVerifier.verify(bytes, new SignatureInfo("HS256", "user", third.sign().sign()));
        assertFalse(confused.valid());
        assertEquals(verifyFail.code(), confused.code());
        assertEquals("UNTRUSTED_KEY_OR_ALGORITHM", confused.message());
        assertFalse(userVerifier.verify(bytes, new SignatureInfo("RSA3072", "unknown", third.sign().sign())).valid());
        assertFalse(serverVerifier.verify(bytes, new SignatureInfo("RSA3072", "server", third.sign().sign())).valid());
        VerificationResult garbage = userVerifier.verify(bytes, new SignatureInfo("RSA3072", "user", "not=a=signature"));
        assertFalse(garbage.valid());
        assertEquals("INVALID_SIGNATURE", garbage.message());
        generator.initialize(2048);
        var weak = generator.generateKeyPair();
        thrown(A4pErrorCode.A4P_INVALID_PARAMETER, () -> Rsa3072.signer("weak", weak.getPrivate()));
    }

    @Test
    void verifierFactoryRejectsMissingAndUntrustworthyKeys() throws Exception {
        KeyPairGenerator generator = KeyPairGenerator.getInstance("RSA");
        generator.initialize(3072);
        PublicKey key = generator.generateKeyPair().getPublic();
        Map<String, PublicKey> nullIdentifier = new HashMap<>();
        nullIdentifier.put(null, key);
        Map<String, PublicKey> nullKey = new HashMap<>();
        nullKey.put("user", null);
        A4pErrorCode invalid = A4pErrorCode.A4P_INVALID_PARAMETER;
        thrown(invalid, () -> Rsa3072.verifier(null));
        thrown(invalid, () -> Rsa3072.verifier(nullIdentifier));
        thrown(invalid, () -> Rsa3072.verifier(nullKey));
        thrown(invalid, () -> Rsa3072.verifier(Map.of(" ", key)));
        generator.initialize(2048);
        PublicKey weak = generator.generateKeyPair().getPublic();
        thrown(invalid, () -> Rsa3072.verifier(Map.of("weak", weak)));
        assertNotNull(Rsa3072.verifier(Map.of("user", key)));
    }
}
