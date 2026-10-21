package io.openjiuwen.a4p;

import io.openjiuwen.a4p.Models.*;
import java.security.PrivateKey;
import java.security.PublicKey;
import java.security.Signature;
import java.security.interfaces.RSAKey;
import java.util.HashMap;
import java.util.Map;

public final class Rsa3072 {
    private Rsa3072() {}

    public static Signer signer(String keyIdentifier, PrivateKey privateKey) {
        Validation.text(keyIdentifier);
        requireKey(privateKey);
        return data -> {
            Signature signer = Signature.getInstance("SHA256withRSA");
            signer.initSign(privateKey);
            signer.update(data);
            return new SignatureInfo(AlgorithmType.RSA3072, keyIdentifier,
                    CanonicalJson.base64UrlEncode(signer.sign()));
        };
    }

    // Codes follow the A4pErrorCode set; message names the reason for diagnostics only.
    public static SignatureVerifier verifier(Map<String, PublicKey> trustedKeys) {
        Validation.present(trustedKeys);
        // Copied into a HashMap first, which admits nulls, so they are rejected as A4pException.
        Map<String, PublicKey> copy = new HashMap<>(trustedKeys);
        copy.forEach((keyIdentifier, key) -> {
            Validation.text(keyIdentifier);
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
                if (verifier.verify(CanonicalJson.base64UrlDecode(info.sign()))) {
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

    private static void requireKey(Object key) {
        Validation.require(key instanceof RSAKey rsa && rsa.getModulus().bitLength() == 3072,
                A4pErrorCode.A4P_INVALID_PARAMETER);
    }
}
