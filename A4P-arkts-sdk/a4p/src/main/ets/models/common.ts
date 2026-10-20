/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

/**
 * Common Types for A4P Protocol
 *
 * Shared interfaces and types used across the A4P SDK,
 * including request/response types and the error type thrown on failure.
 */

import { StructuredIntent } from './intent';
import { UserIdentity, AgentIdentity, ServiceProviderIdentity } from './identity';

/**
 * Request payload for creating a new Level 1 intent.
 */
export interface CreateIntentRequest {
  /** original intent conversion */
  intentConversion: string;
  /** Human-readable summary of the intent */
  intentSummary: string;
  /** Structured intent data containing action and constraints */
  structuredIntent: StructuredIntent;
  /** Pre-assigned intent identifier (optional; generated when absent) */
  intentId?: string;
}

/**
 * Context required for issuing a Level 1 intent to Level 2.
 */
export interface IntentIssueContext {
  /** Identity of the user involved */
  userIdentity: UserIdentity;
  /** Identity of the agent creating the intent */
  agentIdentity: AgentIdentity;
  /** Identity of the service provider */
  serviceProviderIdentity: ServiceProviderIdentity;
}

/**
 * Result of a signature verification operation.
 */
export interface VerificationResult {
  /** Whether the signature is valid */
  valid: boolean;
  /** Verification result code (provider-specific) */
  code: string;
  /** Human-readable result message */
  message?: string;
}

/**
 * A4P Protocol Error Codes.
 *
 * Standard error codes used across all A4P operations.
 *
 * Error code ranges:
 * - 0: Success
 * - 1001-1999: Parameter/Validation errors
 * - 2001-2999: Signature/Verification errors
 * - 3001-3999: State/Lifecycle errors
 * - 4001-4999: Execution/Limit errors
 * - 5001-5999: Serialization/HTTP errors
 * - 6001-6999: System/Internal errors
 */
export enum A4pErrorCode {
  /** Operation completed successfully */
  A4P_SUCCESS = 0,

  /** Request parameter is invalid or missing */
  A4P_INVALID_PARAMETER = 1001,
  /** Protocol version is not supported */
  A4P_UNSUPPORTED_VERSION = 1002,
  /** Identifier already exists (e.g. a duplicate execution ID) */
  A4P_DUPLICATE_ID = 1003,

  /** Signing operation failed */
  A4P_SIGNING_FAIL = 2001,
  /** Signature verification failed */
  A4P_VERIFY_FAIL = 2002,

  /** Intent or execution status does not allow this operation */
  A4P_INVALID_STATUS = 3001,
  /** Timestamp has expired (exceeded validUntil) */
  A4P_TIMESTAMP_EXPIRED = 3002,
  /** Timestamp is too early (before validFrom) */
  A4P_TIMESTAMP_TOO_EARLY = 3003,

  /** Total execution count limit exceeded */
  A4P_EXECUTION_COUNT_EXCEEDED = 4001,
  /** Total amount limit exceeded */
  A4P_AMOUNT_EXCEEDED = 4002,
  /** Period count limit exceeded */
  A4P_PERIOD_COUNT_EXCEEDED = 4003,
  /** Period execution count limit exceeded */
  A4P_PERIOD_EXECUTION_COUNT_EXCEEDED = 4004,
  /** Product quantity limit exceeded */
  A4P_PRODUCT_QUANTITY_EXCEEDED = 4005,
  /** Single order amount limit exceeded */
  A4P_SINGLE_ORDER_AMOUNT_EXCEEDED = 4006,

  /** Serialization failed */
  A4P_SERIALIZE_FAIL = 5001,
  /** Deserialization failed */
  A4P_DESERIALIZE_FAIL = 5002,

  /** Unknown or unexpected internal error */
  A4P_INTERNAL_ERROR = 6001
}

/**
 * The fixed message for an A4P error code: every A4pError thrown with a
 * given code carries exactly this message. Failure-specific causes (which
 * field failed validation, what the underlying error was) are recorded
 * via hilog at the throw site instead of being embedded in the message.
 * @param code - The A4P error code to look up
 * @returns The fixed human-readable message for the code
 */
export function a4pErrorMessage(code: A4pErrorCode): string {
  switch (code) {
    case A4pErrorCode.A4P_SUCCESS:
      return 'Operation completed successfully';
    case A4pErrorCode.A4P_INVALID_PARAMETER:
      return 'Invalid parameter';
    case A4pErrorCode.A4P_UNSUPPORTED_VERSION:
      return 'Unsupported version';
    case A4pErrorCode.A4P_DUPLICATE_ID:
      return 'Duplicate ID';
    case A4pErrorCode.A4P_SIGNING_FAIL:
      return 'Signing failed';
    case A4pErrorCode.A4P_VERIFY_FAIL:
      return 'Verification failed';
    case A4pErrorCode.A4P_INVALID_STATUS:
      return 'Invalid status';
    case A4pErrorCode.A4P_TIMESTAMP_EXPIRED:
      return 'Timestamp expired';
    case A4pErrorCode.A4P_TIMESTAMP_TOO_EARLY:
      return 'Timestamp too early';
    case A4pErrorCode.A4P_EXECUTION_COUNT_EXCEEDED:
      return 'Execution count exceeded';
    case A4pErrorCode.A4P_AMOUNT_EXCEEDED:
      return 'Amount exceeded';
    case A4pErrorCode.A4P_PERIOD_COUNT_EXCEEDED:
      return 'Period count exceeded';
    case A4pErrorCode.A4P_PERIOD_EXECUTION_COUNT_EXCEEDED:
      return 'Period execution count exceeded';
    case A4pErrorCode.A4P_PRODUCT_QUANTITY_EXCEEDED:
      return 'Product quantity exceeded';
    case A4pErrorCode.A4P_SINGLE_ORDER_AMOUNT_EXCEEDED:
      return 'Single order amount exceeded';
    case A4pErrorCode.A4P_SERIALIZE_FAIL:
      return 'Serialization failed';
    case A4pErrorCode.A4P_DESERIALIZE_FAIL:
      return 'Deserialization failed';
    case A4pErrorCode.A4P_INTERNAL_ERROR:
      return 'Unexpected internal error';
    default:
      return 'Unexpected internal error';
  }
}

/**
 * Error thrown by all A4P operations on failure. Follows the HarmonyOS
 * BusinessError shape (see @ohos.base): it extends Error and carries a
 * numeric `code` (an A4pErrorCode), so callers may treat it as a
 * BusinessError<number> — the compatibility is structural, no explicit
 * implements needed. The message is fixed per code (see a4pErrorMessage)
 * — identical codes always carry identical messages. The specific cause
 * of a failure is recorded via hilog at the throw site, never embedded in
 * the message. The `code` field replaces the old A4pResult result-wrapper
 * convention: APIs now return data directly and throw A4pError on
 * failure.
 */
export class A4pError extends Error {
  /** A4P error code, see A4pErrorCode */
  readonly code: A4pErrorCode;

  /**
   * @param code - The A4P error code; the message is derived from it
   */
  constructor(code: A4pErrorCode) {
    // Error message is not writable in ArkTS subclasses; set via the base
    // constructor so `e.message` and `e.toString()` behave as expected.
    super(a4pErrorMessage(code));
    this.name = 'A4pError';
    this.code = code;
  }
}
