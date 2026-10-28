package io.openjiuwen.a4p;

import io.openjiuwen.a4p.Models.SignatureInfo;
import io.openjiuwen.a4p.Models.VerificationResult;

@FunctionalInterface
public interface SignatureVerifier {
    VerificationResult verify(byte[] data, SignatureInfo signature) throws Exception;
}
