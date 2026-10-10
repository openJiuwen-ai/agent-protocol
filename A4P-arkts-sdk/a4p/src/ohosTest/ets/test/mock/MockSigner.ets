/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

import { Signer, SignatureVerifier, VerificationResult, SignatureInfo, AlgorithmType } from '../../../../../Index';
import { base64UrlEncode, base64UrlDecode } from '../../../../main/ets/internal/utils';

/**
 * Deterministic test signer: signs by XOR-ing every byte with 0xFF and
 * base64url-encoding the result.
 */
export class MockSigner implements Signer {
  /** Algorithm reported in the produced signatures */
  private algorithm: AlgorithmType;
  /** Key identifier reported in the produced signatures */
  private keyIdentifier: string;

  constructor(algorithm: AlgorithmType = AlgorithmType.RSA3072, keyIdentifier: string = 'key-001') {
    this.algorithm = algorithm;
    this.keyIdentifier = keyIdentifier;
  }

  sign(data: Uint8Array): SignatureInfo {
    const signature: Uint8Array = new Uint8Array(data.length);
    for (let i = 0; i < data.length; i++) {
      signature[i] = data[i] ^ 0xFF;
    }
    return {
      alg: this.algorithm,
      keyIdentifier: this.keyIdentifier,
      sign: base64UrlEncode(signature),
    };
  }
}

// Keys known to the mock verifier: each resolves to an RSA3072 key, so
// RSA3072 is the only algorithm compatible with any of them.
const KNOWN_RSA3072_KEYS: string[] = ['key-001', 'server-key-001', 'user-key-001'];

/**
 * Test verifier accepting MockSigner signatures; enforces key and
 * algorithm binding per the SignatureVerifier contract.
 */
export class MockVerifier implements SignatureVerifier {
  /** Whether verification should succeed; key and algorithm checks still apply when true */
  private shouldPass: boolean;

  constructor(shouldPass: boolean = true) {
    this.shouldPass = shouldPass;
  }

  verify(data: Uint8Array, signatureInfo: SignatureInfo): VerificationResult {
    if (!this.shouldPass) {
      return {
        valid: false,
        code: '2002',
        message: 'Verification failed by mock'
      };
    }
    // Verifier contract: resolve the key solely from keyIdentifier and
    // reject identifiers this verifier knows nothing about.
    if (KNOWN_RSA3072_KEYS.indexOf(signatureInfo.keyIdentifier) < 0) {
      return {
        valid: false,
        code: '2002',
        message: 'Unknown keyIdentifier: ' + signatureInfo.keyIdentifier
      };
    }
    // Verifier contract: bind alg to the key type resolved from
    // keyIdentifier — any other algorithm is an algorithm-confusion
    // attempt and must be rejected before attempting verification.
    if (signatureInfo.alg !== AlgorithmType.RSA3072) {
      return {
        valid: false,
        code: '2002',
        message: 'Algorithm not compatible with key: ' + signatureInfo.alg
      };
    }
    let signature: Uint8Array;
    try {
      signature = base64UrlDecode(signatureInfo.sign);
    } catch (e) {
      return {
        valid: false,
        code: '2002',
        message: 'Signature is not valid base64url'
      };
    }
    let valid: boolean = signature.length === data.length;
    if (valid) {
      for (let i = 0; i < data.length; i++) {
        if (signature[i] !== (data[i] ^ 0xFF)) {
          valid = false;
          break;
        }
      }
    }
    return {
      valid: valid,
      code: valid ? '0' : '1',
      message: valid ? 'Signature valid' : 'Signature mismatch'
    };
  }
}

/** Test signer that always throws, for exercising signing-failure paths. */
export class FailingSigner implements Signer {
  sign(_data: Uint8Array): SignatureInfo {
    throw new Error('Signing operation failed');
  }
}

/** Test verifier that always throws, for exercising verifier-failure paths. */
export class FailingVerifier implements SignatureVerifier {
  verify(_data: Uint8Array, _signatureInfo: SignatureInfo): VerificationResult {
    throw new Error('Verification operation failed');
  }
}
