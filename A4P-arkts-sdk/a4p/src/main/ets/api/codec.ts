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
  A4pValidationFailure
} from '../internal/validation';

/**
 * Canonical JSON codec for the intent levels and the execution context.
 * Every failure is uniform per direction — serialization throws
 * A4P_SERIALIZE_FAIL, deserialization throws A4P_DESERIALIZE_FAIL — and
 * the specific cause behind the code is recorded via hilog at the throw
 * site, never embedded in the thrown message. Serialization failures — a
 * null input included, since null is invalid data, not an empty document —
 * wrap even a foreign underlying failure (a getter that throws, a cyclic
 * structure exhausting the stack) into the uniform code. Deserialization
 * validates the format only: strict parsing (see parseStrictJson:
 * duplicate keys, keys out of canonical order, explicit nulls,
 * non-canonical number spellings
 * (negative zero included), whitespace, trailing content and malformed
 * literals are rejected instead of being silently last-wins-coalesced by
 * JSON.parse) plus the structural validators, whose closed field sets
 * reject unknown fields at every level. Nothing else is judged: the version is restored
 * as given, and the execution context's ledger, caps and accounting
 * figures are restored verbatim — deserialization returns data, it does
 * not replay the reachability rules the execution APIs enforce on every
 * call, and it passes no version verdict of its own: versions are
 * compared by the chain-facing APIs (issueIntent, signIntent,
 * activateIntent, verifyIntentAuthenticity) and by the execution-class
 * APIs (revokeIntent, createExecution, completeExecution, cancelExecution
 * — each checks the context's own version and its authorized chain on
 * every call), so a restored foreign-version context parses fine but is
 * refused by every operation on it.
 */
export namespace A4pCodec {

  /**
   * Strictly parses serialized JSON for one of the deserialize functions:
   * the parser's own A4pError — with its specific cause already logged
   * (duplicate key, explicit null, trailing content) — is rethrown as-is;
   * only foreign failures get the generic wrapper. A bare "null" document
   * parses to a null root and flows on to the structural validators,
   * whose invalid-data rejection is surfaced as A4P_DESERIALIZE_FAIL —
   * never as a raw TypeError from dereferencing the null root.
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
   * Serializes for one of the serialize functions: canonicalStringify's
   * own A4pError — with its specific cause already logged (a non-plain
   * object, a non-safe integer, a null root or array item) — is rethrown
   * as-is; only foreign failures get the generic wrapper, so a getter that
   * throws or a cyclic structure exhausting the stack still surfaces as
   * the uniform serialization failure instead of leaking a raw
   * TypeError or RangeError.
   */
  function canonicalizeStrictly(value: Object, what: string): string {
    try {
      return canonicalStringify(value);
    } catch (e) {
      if (e instanceof A4pError) {
        throw e;
      }
      a4pThrow(A4pErrorCode.A4P_SERIALIZE_FAIL, 'Failed to serialize ' + what + ': ' + (e as Error).message);
    }
  }

  /**
   * Serializes an IntentL1 to canonical JSON.
   * @param { IntentL1 } intentL1 - The Level 1 intent to serialize.
   * @returns { string } The canonical JSON string representation.
   * @throws { BusinessError } 5001 - Serialization failed (including a null input and any underlying failure).
   */
  export function serializeIntentL1(intentL1: IntentL1): string {
    if (!intentL1) {
      a4pThrow(A4pErrorCode.A4P_SERIALIZE_FAIL, 'intentL1 must not be null');
    }
    return canonicalizeStrictly(intentL1 as Object, 'IntentL1');
  }

  /**
   * Deserializes an IntentL1 from canonical JSON, validating its structure.
   * @param { string } serialized - The canonical JSON string to deserialize.
   * @returns { IntentL1 } The deserialized and validated IntentL1.
   * @throws { BusinessError } 5002 - Deserialization failed (missing input, parse or structure).
   */
  export function deserializeIntentL1(serialized: string): IntentL1 {
    if (!serialized) {
      a4pThrow(A4pErrorCode.A4P_DESERIALIZE_FAIL, 'Serialized string is required');
    }
    const obj: IntentL1 = parseStrictly(serialized, 'IntentL1') as IntentL1;
    const err: A4pValidationFailure | null = validateIntentL1(obj);
    if (err) {
      a4pThrow(A4pErrorCode.A4P_DESERIALIZE_FAIL, err.detail);
    }
    return obj;
  }

  /**
   * Serializes an IntentL2 to canonical JSON.
   * @param { IntentL2 } intentL2 - The Level 2 intent to serialize.
   * @returns { string } The canonical JSON string representation.
   * @throws { BusinessError } 5001 - Serialization failed (including a null input and any underlying failure).
   */
  export function serializeIntentL2(intentL2: IntentL2): string {
    if (!intentL2) {
      a4pThrow(A4pErrorCode.A4P_SERIALIZE_FAIL, 'intentL2 must not be null');
    }
    return canonicalizeStrictly(intentL2 as Object, 'IntentL2');
  }

  /**
   * Deserializes an IntentL2 from canonical JSON, validating its structure.
   * @param { string } serialized - The canonical JSON string to deserialize.
   * @returns { IntentL2 } The deserialized and validated IntentL2.
   * @throws { BusinessError } 5002 - Deserialization failed (missing input, parse or structure).
   */
  export function deserializeIntentL2(serialized: string): IntentL2 {
    if (!serialized) {
      a4pThrow(A4pErrorCode.A4P_DESERIALIZE_FAIL, 'Serialized string is required');
    }
    const obj: IntentL2 = parseStrictly(serialized, 'IntentL2') as IntentL2;
    const err: A4pValidationFailure | null = validateIntentL2(obj);
    if (err) {
      a4pThrow(A4pErrorCode.A4P_DESERIALIZE_FAIL, err.detail);
    }
    return obj;
  }

  /**
   * Serializes an IntentL3 to canonical JSON.
   * @param { IntentL3 } intentL3 - The Level 3 intent to serialize.
   * @returns { string } The canonical JSON string representation.
   * @throws { BusinessError } 5001 - Serialization failed (including a null input and any underlying failure).
   */
  export function serializeIntentL3(intentL3: IntentL3): string {
    if (!intentL3) {
      a4pThrow(A4pErrorCode.A4P_SERIALIZE_FAIL, 'intentL3 must not be null');
    }
    return canonicalizeStrictly(intentL3 as Object, 'IntentL3');
  }

  /**
   * Deserializes an IntentL3 from canonical JSON, validating its structure.
   * @param { string } serialized - The canonical JSON string to deserialize.
   * @returns { IntentL3 } The deserialized and validated IntentL3.
   * @throws { BusinessError } 5002 - Deserialization failed (missing input, parse or structure).
   */
  export function deserializeIntentL3(serialized: string): IntentL3 {
    if (!serialized) {
      a4pThrow(A4pErrorCode.A4P_DESERIALIZE_FAIL, 'Serialized string is required');
    }
    const obj: IntentL3 = parseStrictly(serialized, 'IntentL3') as IntentL3;
    const err: A4pValidationFailure | null = validateIntentL3(obj);
    if (err) {
      a4pThrow(A4pErrorCode.A4P_DESERIALIZE_FAIL, err.detail);
    }
    return obj;
  }

  /**
   * Serializes an IntentExecutionContext to canonical JSON.
   * @param { IntentExecutionContext } context - The execution context to serialize.
   * @returns { string } The canonical JSON string representation.
   * @throws { BusinessError } 5001 - Serialization failed (including a null input and any underlying failure).
   */
  export function serializeIntentExecutionContext(context: IntentExecutionContext): string {
    if (!context) {
      a4pThrow(A4pErrorCode.A4P_SERIALIZE_FAIL, 'context must not be null');
    }
    return canonicalizeStrictly(context as Object, 'IntentExecutionContext');
  }

  /**
   * Deserializes an IntentExecutionContext from canonical JSON, validating its structure.
   * @param { string } serialized - The canonical JSON string to deserialize.
   * @returns { IntentExecutionContext } The deserialized and validated IntentExecutionContext.
   * @throws { BusinessError } 5002 - Deserialization failed (missing input, parse or structure).
   */
  export function deserializeIntentExecutionContext(serialized: string): IntentExecutionContext {
    if (!serialized) {
      a4pThrow(A4pErrorCode.A4P_DESERIALIZE_FAIL, 'Serialized string is required');
    }
    const obj: IntentExecutionContext = parseStrictly(serialized, 'IntentExecutionContext') as IntentExecutionContext;
    const shapeErr: A4pValidationFailure | null = validateIntentExecutionContext(obj);
    if (shapeErr) {
      a4pThrow(A4pErrorCode.A4P_DESERIALIZE_FAIL, shapeErr.detail);
    }
    obj.authorizedIntent = deepFreeze(obj.authorizedIntent);
    return obj;
  }
}
