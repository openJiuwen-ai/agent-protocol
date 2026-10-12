/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */
import { SignatureInfo } from "../models/intent";
import { VerificationResult } from "../models/common"

/**
 * Signature Verifier Interface
 * 
 * Defines the contract for cryptographic signature verification operations.
 */


/**
 * Interface for verifying cryptographic signatures.
 *
 * Contract (security-critical):
 * - `alg` and `keyIdentifier` come from the signed object's detached `sign`
 *   field and are NOT covered by the signature: they remain
 *   attacker-influenced even after `sign.sign` verifies — rewriting them
 *   never invalidates the signature.
 * - Implementations MUST resolve the key solely from `keyIdentifier`,
 *   against keys they trust for the role being verified, and MUST reject
 *   any `alg` value incompatible with the resolved key type
 *   (equivalently: bind each key to its set of allowed algorithms)
 *   before attempting any verification operation. This is the primary
 *   defense against algorithm- and key-confusion attacks (the same class
 *   as unsigned JWT headers, cf. RFC 8725), e.g. rewriting an RSA
 *   signature as HS256 and using the public key bytes as the HMAC secret.
 */
export interface SignatureVerifier {
  /**
   * Verifies a signature against the provided data.
   * @param { Uint8Array } data - The original data that was signed.
   * @param { SignatureInfo } signature - The SignatureInfo containing alg, keyIdentifier, and sign.
   * @returns { VerificationResult } VerificationResult indicating validity and status.
   */
  verify(data: Uint8Array, signature: SignatureInfo): VerificationResult;
}
