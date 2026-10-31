/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

/**
 * Intent Lifecycle Models
 *
 * Defines the core data structures for the A4P intent lifecycle:
 * - L1: Agent-created intent
 * - L2: Issued intent (with user/agent/service provider identities)
 * - L3: User-signed intent (authorized for execution)
 */

import { PaymentConstraints } from './constraint';
import { UserIdentity, AgentIdentity, ServiceProviderIdentity, SigningEnv } from './identity';
import { ShoppingItems } from './product';

/**
 * Actions an intent can authorize.
 */
export enum IntentActionType {
  /** Agent payment: purchase of whitelisted products under payment constraints */
  AI_PAY = 'AI_PAY',
}

/**
 * Signature algorithms supported for intent signing.
 */
export enum AlgorithmType {
  /** RSA signature with a 3072-bit key */
  RSA3072 = 'RSA3072',
}

/**
 * Structured intent data defining the action, the authorized resources,
 * and the payment constraints.
 */
export interface StructuredIntent {
  /** Action to be performed */
  action: IntentActionType;
  /** Resources being acted upon (the authorized purchase whitelist) */
  resources: ShoppingItems;
  /** Payment constraints for this intent */
  constraints: PaymentConstraints;
}

/**
 * Detached signature metadata attached to each intent level: the algorithm
 * and key identifier used to sign this level, plus the signature itself.
 * None of these fields are part of the signed bytes.
 */
export interface SignatureInfo {
  /** Signing algorithm used; detached metadata, not covered by the signature (see sign) */
  alg: AlgorithmType;
  /** Key index used for signing; detached metadata, not covered by the signature (see sign) */
  keyIdentifier: string;
  /**
   * Signer's signature (base64url-encoded, unpadded) over the canonical
   * JSON of the intent with the top-level `sign` object removed entirely.
   * `alg`/`keyIdentifier` sit inside that removed object, so they are NOT
   * covered by the signature: verification reads them before verifying and
   * cannot rely on the signature to authenticate them — the verifier
   * contract (key resolution from `keyIdentifier`, `alg` bound to the key
   * type) is the defense against alg/key confusion.
   */
  sign: string;
}

/**
 * Level 1 Intent - Created by an agent.
 * This is the initial state of an intent before issuance.
 */
export interface IntentL1 {
  /** Intent version */
  version: string;
  /** Unique intent identifier */
  intentId: string;
  /** Intent conversation configuration */
  intentConversation: string;
  /** Human-readable intent summary */
  intentSummary: string;
  /** Structured intent data */
  structuredIntent: StructuredIntent;
  /** Creation timestamp (milliseconds since epoch); must be a non-negative integer */
  timestamp: number;
  /** Agent's signing information, including the signature over this intent */
  sign?: SignatureInfo;
}

/**
 * Level 2 Intent - Issued with identities.
 * Created when a Level 1 intent is issued with user, agent, and service provider information.
 */
export interface IntentL2 {
  /** Intent version */
  version: string;
  /** The original Level 1 intent */
  intentL1: IntentL1;
  /** User identity */
  userIdentity: UserIdentity;
  /** Agent identity */
  agentIdentity: AgentIdentity;
  /** Service provider identity */
  serviceProviderIdentity: ServiceProviderIdentity;
  /** Issuance timestamp (milliseconds since epoch); must be a non-negative integer no earlier than the embedded intentL1's timestamp */
  timestamp: number;
  /** Server's signing information, including the signature over this intent */
  sign?: SignatureInfo;
}

/**
 * Level 3 Intent - User-signed and authorized.
 * Created when a user signs a Level 2 intent, authorizing execution.
 */
export interface IntentL3 {
  /** Intent version */
  version: string;
  /** The Level 2 intent */
  intentL2: IntentL2;
  /** Signing environment information */
  signingEnv: SigningEnv;
  /** Signing timestamp (milliseconds since epoch); must be a non-negative integer no earlier than the embedded intentL2's timestamp */
  timestamp: number;
  /** User's signing information, including the signature over this intent */
  sign?: SignatureInfo;
}
