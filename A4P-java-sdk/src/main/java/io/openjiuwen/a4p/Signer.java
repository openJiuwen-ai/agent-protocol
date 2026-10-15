package io.openjiuwen.a4p;

import io.openjiuwen.a4p.Models.SignatureInfo;

@FunctionalInterface
public interface Signer {
    SignatureInfo sign(byte[] data) throws Exception;
}
