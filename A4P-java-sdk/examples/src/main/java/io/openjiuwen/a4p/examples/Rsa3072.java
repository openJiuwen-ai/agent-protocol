package io.openjiuwen.a4p.examples;

import io.openjiuwen.a4p.*;

import io.openjiuwen.a4p.Models.*;
import java.security.PrivateKey;
import java.security.PublicKey;
import java.security.Signature;
import java.security.interfaces.RSAKey;
import java.util.HashMap;
import java.util.Map;
import java.util.Base64;

public final class Rsa3072 {
    private Rsa3072() {}

    public static Signer signer(String keyIdentifier, PrivateKey privateKey) {
        requireText(keyIdentifier);
        requireKey(privateKey);
        return data -> {
            Signature signer = Signature.getInstance("SHA256withRSA");
            signer.initSign(privateKey);
            signer.update(data);
            return new SignatureInfo(AlgorithmType.RSA3072, keyIdentifier,
                    Base64.getUrlEncoder().withoutPadding().encodeToString(signer.sign()));
        };
    }

    // Codes follow the A4pErrorCode set; message names the reason for diagnostics only.
    public static SignatureVerifier verifier(Map<String, PublicKey> trustedKeys) {
        require(trustedKeys != null);
        // Copied into a HashMap first, which admits nulls, so they are rejected as A4pException.
        Map<String, PublicKey> copy = new HashMap<>(trustedKeys);
        copy.forEach((keyIdentifier, key) -> {
            requireText(keyIdentifier);
            requireKey(key);
        });
        Map<String, PublicKey> keys = Map.copyOf(copy);
        return (data, info) -> {
            if (info == null || !AlgorithmType.RSA3072.name().equals(info.alg())
                    || info.keyIdentifier() == null || !keys.containsKey(info.keyIdentifier())) {
                return rejected("UNTRUSTED_KEY_OR_ALGORITHM");
            }
            try {
                Signature verifier = Signature.getInstance("SHA256withRSA");
                verifier.initVerify(keys.get(info.keyIdentifier()));
                verifier.update(data);
                byte[] signatureBytes = decodeCanonicalSignature(info.sign());
                if (verifier.verify(signatureBytes)) {
                    return new VerificationResult(true, A4pErrorCode.A4P_SUCCESS.code(), null);
                }
                return rejected("INVALID_SIGNATURE");
            } catch (Exception error) {
                return rejected("INVALID_SIGNATURE");
            }
        };
    }

    private static VerificationResult rejected(String reason) {
        return new VerificationResult(false, A4pErrorCode.A4P_VERIFY_FAIL.code(), reason);
    }

    private static byte[] decodeCanonicalSignature(String encoded) {
        if (encoded == null || !encoded.matches("[A-Za-z0-9_-]+")) {
            throw new IllegalArgumentException("Invalid Base64URL signature");
        }
        byte[] decoded = Base64.getUrlDecoder().decode(encoded);
        if (!Base64.getUrlEncoder().withoutPadding().encodeToString(decoded).equals(encoded)) {
            throw new IllegalArgumentException("Noncanonical Base64URL signature");
        }
        return decoded;
    }

    private static void requireText(String value) {
        require(value != null && !value.isBlank());
    }

    private static void require(boolean valid) {
        if (!valid) { throw new A4pException(A4pErrorCode.A4P_INVALID_PARAMETER); }
    }

    private static void requireKey(Object key) {
        require(key instanceof RSAKey rsa && rsa.getModulus().bitLength() == 3072);
    }
}
