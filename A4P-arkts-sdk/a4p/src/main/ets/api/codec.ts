/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

import { A4pError, A4pErrorCode } from '../models/common';
import { IntentL1, IntentL2, IntentL3 } from '../models/intent';
import { IntentExecutionContext } from '../models/execution';
import { a4pThrow } from '../internal/result';
import { canonicalStringify, deepFreeze } from '../internal/utils';
import { parseStrictJson } from '../internal/strictJson';
import {
  validateIntentL1, validateIntentL2, validateIntentL3, validateIntentExecutionContext,
  validateRestoredContext
} from '../internal/validation';

/**
 * Canonical JSON codec for the intent levels and the execution context:
 * serialization produces the canonical form and rejects a null input with
 * A4P_INVALID_PARAMETER — null is invalid data, not an empty document —
 * and deserialization parses and
 * structurally validates the result, so a malformed intent or context
 * never passes through the codec. Parsing is strict (see parseStrictJson):
 * duplicate keys, explicit nulls, numbers not spelled as plain decimal safe integers (fractions and exponents included), trailing
 * content and malformed literals are rejected with A4P_DESERIALIZE_FAIL
 * instead of being silently last-wins-coalesced by JSON.parse, and
 * unknown fields fail structural validation with A4P_INVALID_PARAMETER —
 * the protocol's field sets are closed at every level.
 */
export namespace A4pCodec {

  /**
   * Strictly parses serialized JSON for one of the deserialize functions:
   * the parser's own A4pError — with its specific cause already logged
   * (duplicate key, explicit null, trailing content) — is rethrown as-is;
   * only foreign failures get the generic wrapper. A bare "null" document
   * parses to a null root and flows on to the structural validators,
   * which report it as A4P_INVALID_PARAMETER.
   */
  function parseStrictly(serialized: string, what: string): Object | null {
    try {
      return parseStrictJson(serialized);
    } catch (e) {
      if (e instanceof A4pError) {
        throw e;
      }
      a4pThrow(A4pErrorCode.A4P_DESERIALIZE_FAIL, 'Failed to deserialize ' + what + ': ' + (e as Error).message);
    }
  }

  /**
   * Serializes an IntentL1 to canonical JSON.
   * @param { IntentL1 } intentL1 - The Level 1 intent to serialize.
   * @returns { string } The canonical JSON string representation.
   * @throws { BusinessError } 1001 - Invalid parameter (null input).
   * @throws { BusinessError } 5001 - Serialization failed.
   */
  export function serializeIntentL1(intentL1: IntentL1): string {
    if (!intentL1) {
      a4pThrow(A4pErrorCode.A4P_INVALID_PARAMETER, 'intentL1 must not be null');
    }
    return canonicalStringify(intentL1 as Object);
  }

  /**
   * Deserializes an IntentL1 from canonical JSON, parsing strictly
   * (duplicate keys, explicit nulls, numbers not spelled as plain decimal safe integers (fractions and exponents included) and trailing
   * content fail with 5002)
   * and validating its structure — closed field set included (unknown
   * fields fail with 1001).
   * @param { string } serialized - The canonical JSON string to deserialize.
   * @returns { IntentL1 } The deserialized and validated IntentL1.
   * @throws { BusinessError } 1001 - Invalid parameter.
   * @throws { BusinessError } 5002 - Deserialization failed.
   */
  export function deserializeIntentL1(serialized: string): IntentL1 {
    if (!serialized) {
      a4pThrow(A4pErrorCode.A4P_INVALID_PARAMETER, 'Serialized string is required');
    }
    const obj: IntentL1 = parseStrictly(serialized, 'IntentL1') as IntentL1;
    const validationError = validateIntentL1(obj);
    if (validationError) {
      a4pThrow(validationError.code, validationError.detail);
    }
    return obj;
  }

  /**
   * Serializes an IntentL2 to canonical JSON.
   * @param { IntentL2 } intentL2 - The Level 2 intent to serialize.
   * @returns { string } The canonical JSON string representation.
   * @throws { BusinessError } 1001 - Invalid parameter (null input).
   * @throws { BusinessError } 5001 - Serialization failed.
   */
  export function serializeIntentL2(intentL2: IntentL2): string {
    if (!intentL2) {
      a4pThrow(A4pErrorCode.A4P_INVALID_PARAMETER, 'intentL2 must not be null');
    }
    return canonicalStringify(intentL2 as Object);
  }

  /**
   * Deserializes an IntentL2 from canonical JSON, parsing strictly
   * (duplicate keys, explicit nulls, numbers not spelled as plain decimal safe integers (fractions and exponents included) and trailing
   * content fail with 5002)
   * and validating its structure and embedded L1 — closed field sets
   * included (unknown fields fail with 1001).
   * @param { string } serialized - The canonical JSON string to deserialize.
   * @returns { IntentL2 } The deserialized and validated IntentL2.
   * @throws { BusinessError } 1001 - Invalid parameter.
   * @throws { BusinessError } 5002 - Deserialization failed.
   */
  export function deserializeIntentL2(serialized: string): IntentL2 {
    if (!serialized) {
      a4pThrow(A4pErrorCode.A4P_INVALID_PARAMETER, 'Serialized string is required');
    }
    const obj: IntentL2 = parseStrictly(serialized, 'IntentL2') as IntentL2;
    const validationError = validateIntentL2(obj);
    if (validationError) {
      a4pThrow(validationError.code, validationError.detail);
    }
    return obj;
  }

  /**
   * Serializes an IntentL3 to canonical JSON.
   * @param { IntentL3 } intentL3 - The Level 3 intent to serialize.
   * @returns { string } The canonical JSON string representation.
   * @throws { BusinessError } 1001 - Invalid parameter (null input).
   * @throws { BusinessError } 5001 - Serialization failed.
   */
  export function serializeIntentL3(intentL3: IntentL3): string {
    if (!intentL3) {
      a4pThrow(A4pErrorCode.A4P_INVALID_PARAMETER, 'intentL3 must not be null');
    }
    return canonicalStringify(intentL3 as Object);
  }

  /**
   * Deserializes an IntentL3 from canonical JSON, parsing strictly
   * (duplicate keys, explicit nulls, numbers not spelled as plain decimal safe integers (fractions and exponents included) and trailing
   * content fail with 5002)
   * and validating its structure and embedded L2/L1 — closed field sets
   * included (unknown fields fail with 1001).
   * @param { string } serialized - The canonical JSON string to deserialize.
   * @returns { IntentL3 } The deserialized and validated IntentL3.
   * @throws { BusinessError } 1001 - Invalid parameter.
   * @throws { BusinessError } 5002 - Deserialization failed.
   */
  export function deserializeIntentL3(serialized: string): IntentL3 {
    if (!serialized) {
      a4pThrow(A4pErrorCode.A4P_INVALID_PARAMETER, 'Serialized string is required');
    }
    const obj: IntentL3 = parseStrictly(serialized, 'IntentL3') as IntentL3;
    const validationError = validateIntentL3(obj);
    if (validationError) {
      a4pThrow(validationError.code, validationError.detail);
    }
    return obj;
  }

  /**
   * Serializes an IntentExecutionContext to canonical JSON.
   * @param { IntentExecutionContext } context - The execution context to serialize.
   * @returns { string } The canonical JSON string representation.
   * @throws { BusinessError } 1001 - Invalid parameter (null input).
   * @throws { BusinessError } 5001 - Serialization failed.
   */
  export function serializeIntentExecutionContext(context: IntentExecutionContext): string {
    if (!context) {
      a4pThrow(A4pErrorCode.A4P_INVALID_PARAMETER, 'context must not be null');
    }
    return canonicalStringify(context as Object);
  }

  /**
   * Deserializes an IntentExecutionContext from canonical JSON, parsing
   * strictly (duplicate keys, explicit nulls, numbers not spelled as plain decimal safe integers (fractions and exponents included)
   * and trailing content fail with 5002) and validating in two passes.
   * The shape pass (see
   * validateIntentExecutionContext) checks the authorized intent chain,
   * the intentId derivation, the status-bound revokedTime, every ledger
   * record, and the accounting
   * snapshot — closed field sets included. The content pass (see
   * validateRestoredContext) then replays the reachability rules the
   * lifecycle APIs enforce: the activatedTime against the validity
   * window (the chain's stamps carry no cross-level ordering — each
   * level's interface runs in a different process), every record's
   * reservation instant against the activation floor and the window, each
   * record's period index against its own reservation instant, id uniqueness,
   * per-record parameter consistency with the signed snapshot (action,
   * currency, single-order cap, cart scope and template), the
   * accumulated caps, the status agreement for the clock-independent
   * statuses, and the accounting snapshot against a re-derivation from
   * the ledger — a violation fails with the code the offending
   * operation would have returned. The authorization snapshot is
   * re-frozen on the way out, preserving the activation invariant —
   * execution checks read an immutable snapshot while the ledger and the
   * accounting figures stay mutable.
   * @param { string } serialized - The canonical JSON string to deserialize.
   * @returns { IntentExecutionContext } The deserialized and validated IntentExecutionContext.
   * @throws { BusinessError } 1001 - Invalid parameter.
   * @throws { BusinessError } 1002 - Unsupported version.
   * @throws { BusinessError } 1003 - Duplicate ID.
   * @throws { BusinessError } 3002 - Timestamp expired.
   * @throws { BusinessError } 3003 - Timestamp too early.
   * @throws { BusinessError } 4001 - Execution count exceeded.
   * @throws { BusinessError } 4002 - Amount exceeded.
   * @throws { BusinessError } 4004 - Period execution count exceeded.
   * @throws { BusinessError } 4005 - Cart does not match the authorized resource.
   * @throws { BusinessError } 4006 - Single order amount exceeded.
   * @throws { BusinessError } 4007 - Product not authorized.
   * @throws { BusinessError } 5002 - Deserialization failed.
   */
  export function deserializeIntentExecutionContext(serialized: string): IntentExecutionContext {
    if (!serialized) {
      a4pThrow(A4pErrorCode.A4P_INVALID_PARAMETER, 'Serialized string is required');
    }
    const obj: IntentExecutionContext = parseStrictly(serialized, 'IntentExecutionContext') as IntentExecutionContext;
    const validationError = validateIntentExecutionContext(obj);
    if (validationError) {
      a4pThrow(validationError.code, validationError.detail);
    }
    const restoreError = validateRestoredContext(obj);
    if (restoreError) {
      a4pThrow(restoreError.code, restoreError.detail);
    }
    obj.authorizedIntent = deepFreeze(obj.authorizedIntent);
    return obj;
  }
}
