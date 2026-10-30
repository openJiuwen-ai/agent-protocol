package io.openjiuwen.a4p;

import io.openjiuwen.a4p.Models.SignatureInfo;

/**
 * Supplies application-owned signing infrastructure without exposing private keys to the SDK.
 * The SDK passes canonical UTF-8 bytes with only the current layer signature removed;
 * signatures of embedded intent layers remain covered.
 * @see SignatureVerifier
 */
@FunctionalInterface
public interface Signer {
    /**
     * Signs SDK-provided canonical UTF-8 intent bytes.
     * Return a supported algorithm, trusted key identifier and unpadded Base64URL signature.
     * Invalid metadata or callback exceptions become A4P_SIGNING_FAIL in lifecycle operations.
     * @param data canonical UTF-8 intent bytes excluding the current layer signature
     * @return signature metadata
     * @throws Exception if the application signing or verification infrastructure fails
     */
    SignatureInfo sign(byte[] data) throws Exception;
}
