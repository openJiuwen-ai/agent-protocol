/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

/**
 * Identity Models
 *
 * Defines the identities attached to an issued (L2) intent: the paying
 * user, the agent acting on their behalf, and the service provider; plus
 * the environment of the user's signing device (recorded on L3).
 */

/**
 * Identity of the paying user, as attested by an identity provider.
 */
export interface UserIdentity {
  /** Identifier of the identity provider attesting this identity */
  identityProvider: string;
  /** User identifier, unique within the provider */
  uid: string;
  /** Additional identity information (JSON string); optional, absent means none */
  additionalInfo?: string;
}

/**
 * Identity of the agent creating the intent and acting for the user.
 */
export interface AgentIdentity {
  /** Identifier of the identity provider attesting this identity */
  identityProvider: string;
  /** Agent identifier, unique within the provider */
  id: string;
  /** Additional identity information (JSON string); optional, absent means none */
  additionalInfo?: string;
}

/**
 * Identity of the service provider processing the payment.
 */
export interface ServiceProviderIdentity {
  /** Service provider identifier */
  id: string;
  /** Additional identity information (JSON string); optional, absent means none */
  additionalInfo?: string;
}

/**
 * Environment of the user's device at signing time, recorded on the
 * Level 3 intent.
 */
export interface SigningEnv {
  /** Device information (e.g. model and OS version) */
  device: object;
  /** Application information (e.g. name and version) */
  app: object;
}
