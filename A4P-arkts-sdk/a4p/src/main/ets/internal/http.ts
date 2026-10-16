/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

import { A4pError, A4pErrorCode } from '../models/common';
import { IntentL1, IntentL2, IntentL3 } from '../models/intent';
import { A4pCodec } from './codec';
import { A4pValidationFailure } from './validation';
import { a4pThrow } from './result';
import { logErrorDetail } from './log';
import { encodeUtf8, decodeUtf8 } from './utils';

/**
 * A transport-independent HTTP request. The builders produce this
 * shape instead of binding to a platform-specific HTTP client.
 */
export interface A4pHttpRequest {
  /** HTTP method (A4P endpoints use POST) */
  method: string;
  /** Request path (e.g. '/a4p/v1/intent/proposal') */
  path: string;
  /** HTTP headers */
  headers: Map<string, string>;
  /** Raw request body bytes */
  body: Uint8Array;
}

/**
 * A transport-independent HTTP response, paired with A4pHttpRequest.
 */
export interface A4pHttpResponse {
  /** HTTP status code */
  statusCode: number;
  /** HTTP headers */
  headers: Map<string, string>;
  /** Raw response body bytes */
  body: Uint8Array;
}

/**
 * Payload returned by the submission endpoint after an intent has
 * been activated.
 */
export interface IntentActivationResponse {
  /** Activation status (e.g. "activated") */
  status: string;
  /** Human-readable result message */
  message: string;
}

const CONTENT_TYPE_JSON: string = 'application/a4p+json';
const PATH_PROPOSAL: string = '/a4p/v1/intent/proposal';
const PATH_AUTHORIZATION: string = '/a4p/v1/intent/authorization';
const PATH_SUBMISSION: string = '/a4p/v1/intent/submission';

/**
 * Maps an A4P error code to the closest HTTP status class:
 * parameter errors → 400, signature failures → 401, state conflicts and
 * duplicate IDs → 409, unsupported version → 426, everything else → 500.
 * @param code - The A4P error code to map
 * @returns The corresponding HTTP status code
 */
function statusCodeFor(code: A4pErrorCode): number {
  if (code === A4pErrorCode.A4P_VERIFY_FAIL || code === A4pErrorCode.A4P_SIGNING_FAIL) {
    return 401;
  }
  if (code === A4pErrorCode.A4P_INVALID_STATUS || code === A4pErrorCode.A4P_DUPLICATE_ID) {
    return 409;
  }
  if (code === A4pErrorCode.A4P_UNSUPPORTED_VERSION) {
    return 426;
  }
  if (code >= A4pErrorCode.A4P_INVALID_PARAMETER && code <= A4pErrorCode.A4P_TIMESTAMP_TOO_EARLY) {
    return 400;
  }
  return 500;
}

/**
 * Whether a numeric code is a defined A4pErrorCode failure member.
 * @param code - The numeric code to check
 * @returns True if the code is a known A4P error code (excluding A4P_SUCCESS)
 */
function isKnownErrorCode(code: number): boolean {
  // Code 0 is A4P_SUCCESS, not a failure; a failure body must never carry
  // it (a body with code 0 is a malformed failure).
  return code !== A4pErrorCode.A4P_SUCCESS && code in A4pErrorCode;
}

/**
 * Builds an HTTP request with the given path and JSON body.
 * @param path - The request path
 * @param bodyJson - The JSON string to use as the request body
 * @returns The constructed HTTP request
 */
function buildRequest(path: string, bodyJson: string): A4pHttpRequest {
  const headers: Map<string, string> = new Map<string, string>();
  headers.set('Content-Type', CONTENT_TYPE_JSON);
  return {
    method: 'POST',
    path: path,
    headers: headers,
    body: encodeUtf8(bodyJson),
  };
}

/**
 * Builds an HTTP response with the given status code and JSON body.
 * @param statusCode - The HTTP status code
 * @param bodyJson - The JSON string to use as the response body
 * @returns The constructed HTTP response
 */
function buildResponse(statusCode: number, bodyJson: string): A4pHttpResponse {
  const headers: Map<string, string> = new Map<string, string>();
  headers.set('Content-Type', CONTENT_TYPE_JSON);
  return {
    statusCode: statusCode,
    headers: headers,
    body: encodeUtf8(bodyJson),
  };
}

/**
 * Decodes a UTF-8 byte array body into a string.
 * @param body - The byte array to decode
 * @returns The decoded string
 */
function parseBody(body: Uint8Array): string {
  return decodeUtf8(body);
}

/**
 * Builds a success response: the JSON envelope `{"data": <payload>}`.
 * @param data - The payload to wrap in the data envelope
 * @returns The HTTP 200 response with the data envelope
 */
function buildDataResponse<T>(data: T): A4pHttpResponse {
  const wrapper: object = {
    data: data === undefined ? null : data,
  };
  return buildResponse(200, JSON.stringify(wrapper));
}

/**
 * Builds a failure response from a thrown A4pError: the JSON envelope
 * `{"code": ..., "message": ...}` with the mapped HTTP status code.
 * @param error - The A4pError to encode into the response
 * @returns The HTTP error response with the error envelope
 */
export function buildErrorResponse(error: A4pError): A4pHttpResponse {
  const wrapper: object = {
    code: error.code,
    message: error.message,
  };
  return buildResponse(statusCodeFor(error.code), JSON.stringify(wrapper));
}

/**
 * Parses a response envelope. Success (2xx with a data field) returns the
 * data; any failure throws A4pError. Fail-closed: a 2xx body without an
 * explicit data field is not a success.
 * @param response - The HTTP response to parse
 * @returns The data payload extracted from the response envelope
 * @throws A4pError on non-2xx status, missing data field, or parse failure
 */
function parseDataResponse<T>(response: A4pHttpResponse): T {
  if (response.statusCode < 200 || response.statusCode >= 300) {
    let error: A4pError;
    try {
      const parsed: Record<string, Object> = JSON.parse(parseBody(response.body)) as Record<string, Object>;
      error = parseErrorEnvelope(parsed, response.statusCode);
    } catch (e) {
      logErrorDetail(A4pErrorCode.A4P_INTERNAL_ERROR,
        'HTTP status ' + response.statusCode + ', unparseable error body: ' + (e as Error).message);
      error = new A4pError(A4pErrorCode.A4P_INTERNAL_ERROR);
    }
    throw error;
  }
  let parsed: Record<string, Object>;
  try {
    parsed = JSON.parse(parseBody(response.body)) as Record<string, Object>;
  } catch (e) {
    a4pThrow(A4pErrorCode.A4P_DESERIALIZE_FAIL, 'Failed to parse response body: ' + (e as Error).message);
  }
  // Fail closed: a 2xx body without an explicit data field is not a
  // success, regardless of the status.
  if (!Object.prototype.hasOwnProperty.call(parsed, 'data')) {
    const error: A4pError = parseErrorEnvelopeIfPresent(parsed);
    if (error) {
      throw error;
    }
    a4pThrow(A4pErrorCode.A4P_DESERIALIZE_FAIL, 'Response body has no data field');
  }
  return parsed['data'] as T;
}

/**
 * Builds an A4pError from an error envelope `{code, message}`. The remote
 * message describes the specific cause and is recorded via hilog; the
 * returned error carries the fixed message for its code.
 * @param parsed - The parsed JSON object containing code and message fields
 * @param statusCode - The HTTP status code to use as fallback detail
 * @returns The constructed A4pError
 */
function parseErrorEnvelope(parsed: Record<string, Object>, statusCode: number): A4pError {
  const rawCode: number = parsed['code'] as number;
  // Only propagate codes that are defined A4P error codes; anything else
  // on the wire is not ours and must not leak into our enum.
  const code: A4pErrorCode = (typeof rawCode === 'number' && isKnownErrorCode(rawCode))
    ? rawCode as A4pErrorCode : A4pErrorCode.A4P_INTERNAL_ERROR;
  const remoteMessage: string = (parsed['message'] as string) ?? 'HTTP error ' + statusCode;
  logErrorDetail(code, 'remote error message: ' + remoteMessage);
  return new A4pError(code);
}

/**
 * If the body is an error envelope `{code, message}` (no data field),
 * returns the corresponding A4pError; otherwise null.
 * @param parsed - The parsed JSON object to inspect for an error envelope
 * @returns The A4pError if an error envelope is present, or null otherwise
 */
function parseErrorEnvelopeIfPresent(parsed: Record<string, Object>): A4pError | null {
  if (!Object.prototype.hasOwnProperty.call(parsed, 'code')) {
    return null;
  }
  return parseErrorEnvelope(parsed, 200);
}

/**
 * Validates that an IntentActivationResponse has the required status field.
 * @param data - The activation response to validate
 * @returns A validation failure if status is missing, or null if valid
 */
function validateIntentActivationResponse(data: IntentActivationResponse): A4pValidationFailure | null {
  if (!data.status) {
    return new A4pValidationFailure(A4pErrorCode.A4P_DESERIALIZE_FAIL, 'Activation response is missing status');
  }
  return null;
}

/**
 * Builders and parsers for the three A4P HTTP flows: proposal (L1 to
 * L2), authorization (L2 to L3), and submission (L3 to activation).
 * Request bodies carry the canonical JSON of the intent; success
 * responses wrap the payload in a data envelope, error responses carry
 * a code/message envelope (see buildErrorResponse).
 */
export namespace A4pHttp {

  /**
   * Builds the proposal request carrying the L1 intent.
   * @param intentL1 - The Level 1 intent to include in the proposal request
   * @returns The HTTP request for the proposal endpoint
   * @throws A4pError with A4P_SERIALIZE_FAIL when serialization fails
   */
  export function buildIntentProposalRequest(intentL1: IntentL1): A4pHttpRequest {
    return buildRequest(PATH_PROPOSAL, A4pCodec.serializeIntentL1(intentL1));
  }

  /**
   * Parses a proposal request into an IntentL1.
   * @param request - The HTTP request to parse
   * @returns The deserialized and validated IntentL1
   * @throws A4pError on deserialization/validation failure
   */
  export function parseIntentProposalRequest(request: A4pHttpRequest): IntentL1 {
    try {
      return A4pCodec.deserializeIntentL1(parseBody(request.body));
    } catch (e) {
      if (e instanceof A4pError) {
        throw e;
      }
      a4pThrow(A4pErrorCode.A4P_DESERIALIZE_FAIL, 'Failed to parse proposal request: ' + (e as Error).message);
    }
  }

  /**
   * Builds the success proposal response carrying the L2 intent.
   * @param intentL2 - The Level 2 intent to include in the proposal response
   * @returns The HTTP 200 response with the serialized L2 intent
   */
  export function buildIntentProposalResponse(intentL2: IntentL2): A4pHttpResponse {
    return buildDataResponse(A4pCodec.serializeIntentL2(intentL2));
  }

  /**
   * Parses a proposal response into an IntentL2.
   * @param response - The HTTP response to parse
   * @returns The deserialized and validated IntentL2
   * @throws A4pError on transport, envelope, or validation failure
   */
  export function parseIntentProposalResponse(response: A4pHttpResponse): IntentL2 {
    const bodyStr: string = parseDataResponse<string>(response);
    return A4pCodec.deserializeIntentL2(bodyStr);
  }

  /**
   * Builds the authorization request carrying the L2 intent.
   * @param intentL2 - The Level 2 intent to include in the authorization request
   * @returns The HTTP request for the authorization endpoint
   * @throws A4pError with A4P_SERIALIZE_FAIL when serialization fails
   */
  export function buildIntentAuthorizationRequest(intentL2: IntentL2): A4pHttpRequest {
    return buildRequest(PATH_AUTHORIZATION, A4pCodec.serializeIntentL2(intentL2));
  }

  /**
   * Parses an authorization request into an IntentL2.
   * @param request - The HTTP request to parse
   * @returns The deserialized and validated IntentL2
   * @throws A4pError on deserialization/validation failure
   */
  export function parseIntentAuthorizationRequest(request: A4pHttpRequest): IntentL2 {
    try {
      return A4pCodec.deserializeIntentL2(parseBody(request.body));
    } catch (e) {
      if (e instanceof A4pError) {
        throw e;
      }
      a4pThrow(A4pErrorCode.A4P_DESERIALIZE_FAIL, 'Failed to parse authorization request: ' + (e as Error).message);
    }
  }

  /**
   * Builds the success authorization response carrying the L3 intent.
   * @param intentL3 - The Level 3 intent to include in the authorization response
   * @returns The HTTP 200 response with the serialized L3 intent
   */
  export function buildIntentAuthorizationResponse(intentL3: IntentL3): A4pHttpResponse {
    return buildDataResponse(A4pCodec.serializeIntentL3(intentL3));
  }

  /**
   * Parses an authorization response into an IntentL3.
   * @param response - The HTTP response to parse
   * @returns The deserialized and validated IntentL3
   * @throws A4pError on transport, envelope, or validation failure
   */
  export function parseIntentAuthorizationResponse(response: A4pHttpResponse): IntentL3 {
    const bodyStr: string = parseDataResponse<string>(response);
    return A4pCodec.deserializeIntentL3(bodyStr);
  }

  /**
   * Builds the submission request carrying the L3 intent.
   * @param intentL3 - The Level 3 intent to include in the submission request
   * @returns The HTTP request for the submission endpoint
   * @throws A4pError with A4P_SERIALIZE_FAIL when serialization fails
   */
  export function buildIntentSubmissionRequest(intentL3: IntentL3): A4pHttpRequest {
    return buildRequest(PATH_SUBMISSION, A4pCodec.serializeIntentL3(intentL3));
  }

  /**
   * Parses a submission request into an IntentL3.
   * @param request - The HTTP request to parse
   * @returns The deserialized and validated IntentL3
   * @throws A4pError on deserialization/validation failure
   */
  export function parseIntentSubmissionRequest(request: A4pHttpRequest): IntentL3 {
    try {
      return A4pCodec.deserializeIntentL3(parseBody(request.body));
    } catch (e) {
      if (e instanceof A4pError) {
        throw e;
      }
      a4pThrow(A4pErrorCode.A4P_DESERIALIZE_FAIL, 'Failed to parse submission request: ' + (e as Error).message);
    }
  }

  /**
   * Builds the success submission response carrying the activation result.
   * @param activation - The activation result to include in the submission response
   * @returns The HTTP 200 response with the activation result
   */
  export function buildIntentSubmissionResponse(activation: IntentActivationResponse): A4pHttpResponse {
    return buildDataResponse(activation);
  }

  /**
   * Parses a submission response into an IntentActivationResponse.
   * @param response - The HTTP response to parse
   * @returns The deserialized and validated activation response
   * @throws A4pError on transport, envelope, or validation failure
   */
  export function parseIntentSubmissionResponse(response: A4pHttpResponse): IntentActivationResponse {
    const data: IntentActivationResponse = parseDataResponse<IntentActivationResponse>(response);
    const validationError = validateIntentActivationResponse(data);
    if (validationError) {
      a4pThrow(validationError.code, validationError.detail);
    }
    return data;
  }
}
