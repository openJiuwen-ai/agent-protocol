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
 * A signer that throws — or a null one — fails the calling API with
 * A4P_SIGNING_FAIL (an infrastructure failure, matching the Java SDK,
 * which maps the NullPointerException of the same call to that code);
 * one that returns nothing, or a malformed SignatureInfo (undefined
 * `alg`, blank `keyIdentifier` or `sign`), has produced bad data at the
 * boundary and fails the calling API with A4P_INVALID_PARAMETER — the
 * result is validated before it is attached to the intent (see runSigner
 * in api/a4p.ts), mirroring the verifier-side guard on a verifier's
 * return value.
 */
export interface Signer {
  /**
   * Signs the provided data bytes.
   * @param { Uint8Array } data - The data to sign as a byte array.
   * @returns { SignatureInfo } The SignatureInfo containing alg, keyIdentifier, and sign.
   */
  sign(data: Uint8Array): SignatureInfo;
}
