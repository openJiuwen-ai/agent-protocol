package io.openjiuwen.a4p;

import io.openjiuwen.a4p.Models.SignatureInfo;
import io.openjiuwen.a4p.Models.VerificationResult;

/**
 * Supplies application-owned signature verification using trusted keys.
 * Resolve keyIdentifier against a trusted registry, enforce the algorithm bound to that key,
 * and decode the signature as unpadded Base64URL. Do not trust the outer signature algorithm metadata alone.
 * The SDK passes canonical UTF-8 bytes excluding only the current layer signature.
 * @see Signer
 */
@FunctionalInterface
public interface SignatureVerifier {
    /**
     * Verifies SDK-provided canonical bytes against a signature using trusted key infrastructure.
     * Resolve the key and its algorithm independently of untrusted metadata.
     * The valid flag must be true exactly when code is zero; callback exceptions and inconsistent verdicts
     * become A4P_VERIFY_FAIL in lifecycle operations.
     * @param data canonical UTF-8 intent bytes excluding the current layer signature
     * @param signature signature metadata to verify
     * @return signature verification verdict
     * @throws Exception if the application signing or verification infrastructure fails
     */
    VerificationResult verify(byte[] data, SignatureInfo signature) throws Exception;
}
