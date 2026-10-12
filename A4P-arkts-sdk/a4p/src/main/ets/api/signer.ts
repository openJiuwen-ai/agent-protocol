/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */
import { SignatureInfo } from "../models/intent";

/**
 * Signer Interface
 * 
 * Defines the contract for cryptographic signing operations.
 * Implementations provide algorithm info, key identification, and signing capability.
 */

/**
 * Interface for signing data with a cryptographic key.
 *
 * Contract: implementations return an `alg` identifier and a `keyIdentifier`
 * consistent with the key actually used for signing. Both are detached
 * metadata, not covered by the signature — verifiers bind the algorithm
 * set to the key resolved from `keyIdentifier` (see SignatureVerifier),
 * so a mismatched pair fails verification.
 *
 * A signer that throws, returns nothing, or returns a malformed
 * SignatureInfo (undefined `alg`, blank `keyIdentifier` or `sign`) fails
 * the calling API with A4P_SIGNING_FAIL — the result is validated before
 * it is attached to the intent (see runSigner in api/a4p.ts), mirroring
 * the verifier-side guard on a verifier's return value.
 */
export interface Signer {
  /**
   * Signs the provided data bytes.
   * @param data - The data to sign as a byte array
   * @returns The SignatureInfo containing alg, keyIdentifier, and sign
   */
  sign(data: Uint8Array): SignatureInfo;
}
