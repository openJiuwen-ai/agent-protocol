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
 * consistent with the key actually used for signing — verifiers bind the
 * algorithm set to the key resolved from `keyIdentifier` (see
 * SignatureVerifier), so a mismatched pair fails verification.
 */
export interface Signer {
  /**
   * Signs the provided data bytes.
   * @param data - The data to sign as a byte array
   * @returns The SignatureInfo containing alg, keyIdentifier, and sign
   */
  sign(data: Uint8Array): SignatureInfo;
}
