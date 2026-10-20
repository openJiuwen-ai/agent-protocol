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
 * - `alg` and `keyIdentifier` come from the signed object's own fields, so a
 *   successful verification retroactively authenticates them. They are
 *   nevertheless attacker-influenced inputs until verification succeeds.
 * - Implementations MUST resolve the key solely from `keyIdentifier` and MUST
 *   reject any `alg` value incompatible with the resolved key type
 *   (equivalently: bind each key to its set of allowed algorithms)
 *   before attempting any verification operation. This prevents
 *   algorithm-confusion attacks, e.g. rewriting an RSA signature as
 *   HS256 and using the public key bytes as the HMAC secret.
 */
export interface SignatureVerifier {
  /**
   * Verifies a signature against the provided data.
   * @param data - The original data that was signed
   * @param signature - The SignatureInfo containing alg, keyIdentifier, and sign
   * @returns VerificationResult indicating validity and status
   */
  verify(data: Uint8Array, signature: SignatureInfo): VerificationResult;
}
