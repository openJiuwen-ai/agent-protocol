/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

import { A4pErrorCode } from '../models/common';
import { IntentL1, IntentL2, IntentL3 } from '../models/intent';
import { IntentExecutionContext } from '../models/execution';
import { a4pThrow } from '../internal/result';
import { canonicalStringify, deepFreeze } from '../internal/utils';
import {
  validateIntentL1, validateIntentL2, validateIntentL3, validateIntentExecutionContext
} from '../internal/validation';

/**
 * Canonical JSON codec for the intent levels and the execution context:
 * serialization produces the canonical form, deserialization parses and
 * structurally validates the result, so a malformed intent or context
 * never passes through the codec.
 */
export namespace A4pCodec {

  /**
   * Serializes an IntentL1 to canonical JSON.
   * @param intentL1 - The Level 1 intent to serialize
   * @returns The canonical JSON string representation
   * @throws A4pError with A4P_SERIALIZE_FAIL on failure
   */
  export function serializeIntentL1(intentL1: IntentL1): string {
    return canonicalStringify(intentL1 as Object);
  }

  /**
   * Deserializes an IntentL1 from canonical JSON, validating its structure.
   * @param serialized - The canonical JSON string to deserialize
   * @returns The deserialized and validated IntentL1
   * @throws A4pError with A4P_INVALID_PARAMETER (empty input / invalid
   *         structure) or A4P_DESERIALIZE_FAIL (invalid JSON)
   */
  export function deserializeIntentL1(serialized: string): IntentL1 {
    if (!serialized) {
      a4pThrow(A4pErrorCode.A4P_INVALID_PARAMETER, 'Serialized string is required');
    }
    let obj: IntentL1;
    try {
      obj = JSON.parse(serialized) as IntentL1;
    } catch (e) {
      a4pThrow(A4pErrorCode.A4P_DESERIALIZE_FAIL, 'Failed to deserialize IntentL1: ' + (e as Error).message);
    }
    const validationError = validateIntentL1(obj);
    if (validationError) {
      a4pThrow(validationError.code, validationError.detail);
    }
    return obj;
  }

  /**
   * Serializes an IntentL2 to canonical JSON.
   * @param intentL2 - The Level 2 intent to serialize
   * @returns The canonical JSON string representation
   * @throws A4pError with A4P_SERIALIZE_FAIL on failure
   */
  export function serializeIntentL2(intentL2: IntentL2): string {
    return canonicalStringify(intentL2 as Object);
  }

  /**
   * Deserializes an IntentL2 from canonical JSON, validating its structure.
   * @param serialized - The canonical JSON string to deserialize
   * @returns The deserialized and validated IntentL2
   * @throws A4pError with A4P_INVALID_PARAMETER (empty input / invalid
   *         structure) or A4P_DESERIALIZE_FAIL (invalid JSON)
   */
  export function deserializeIntentL2(serialized: string): IntentL2 {
    if (!serialized) {
      a4pThrow(A4pErrorCode.A4P_INVALID_PARAMETER, 'Serialized string is required');
    }
    let obj: IntentL2;
    try {
      obj = JSON.parse(serialized) as IntentL2;
    } catch (e) {
      a4pThrow(A4pErrorCode.A4P_DESERIALIZE_FAIL, 'Failed to deserialize IntentL2: ' + (e as Error).message);
    }
    const validationError = validateIntentL2(obj);
    if (validationError) {
      a4pThrow(validationError.code, validationError.detail);
    }
    return obj;
  }

  /**
   * Serializes an IntentL3 to canonical JSON.
   * @param intentL3 - The Level 3 intent to serialize
   * @returns The canonical JSON string representation
   * @throws A4pError with A4P_SERIALIZE_FAIL on failure
   */
  export function serializeIntentL3(intentL3: IntentL3): string {
    return canonicalStringify(intentL3 as Object);
  }

  /**
   * Deserializes an IntentL3 from canonical JSON, validating its structure.
   * @param serialized - The canonical JSON string to deserialize
   * @returns The deserialized and validated IntentL3
   * @throws A4pError with A4P_INVALID_PARAMETER (empty input / invalid
   *         structure) or A4P_DESERIALIZE_FAIL (invalid JSON)
   */
  export function deserializeIntentL3(serialized: string): IntentL3 {
    if (!serialized) {
      a4pThrow(A4pErrorCode.A4P_INVALID_PARAMETER, 'Serialized string is required');
    }
    let obj: IntentL3;
    try {
      obj = JSON.parse(serialized) as IntentL3;
    } catch (e) {
      a4pThrow(A4pErrorCode.A4P_DESERIALIZE_FAIL, 'Failed to deserialize IntentL3: ' + (e as Error).message);
    }
    const validationError = validateIntentL3(obj);
    if (validationError) {
      a4pThrow(validationError.code, validationError.detail);
    }
    return obj;
  }

  /**
   * Serializes an IntentExecutionContext to canonical JSON.
   * @param context - The execution context to serialize
   * @returns The canonical JSON string representation
   * @throws A4pError with A4P_SERIALIZE_FAIL on failure
   */
  export function serializeIntentExecutionContext(context: IntentExecutionContext): string {
    return canonicalStringify(context as Object);
  }

  /**
   * Deserializes an IntentExecutionContext from canonical JSON, validating
   * its structure: the authorized intent chain, the intentId derivation,
   * every ledger record, and the accounting snapshot (see
   * validateIntentExecutionContext). The authorization snapshot is
   * re-frozen on the way out, preserving the activation invariant —
   * execution checks read an immutable snapshot while the ledger and the
   * accounting figures stay mutable.
   * @param serialized - The canonical JSON string to deserialize
   * @returns The deserialized and validated IntentExecutionContext
   * @throws A4pError with A4P_INVALID_PARAMETER (empty input / invalid
   *         structure) or A4P_DESERIALIZE_FAIL (invalid JSON)
   */
  export function deserializeIntentExecutionContext(serialized: string): IntentExecutionContext {
    if (!serialized) {
      a4pThrow(A4pErrorCode.A4P_INVALID_PARAMETER, 'Serialized string is required');
    }
    let obj: IntentExecutionContext;
    try {
      obj = JSON.parse(serialized) as IntentExecutionContext;
    } catch (e) {
      a4pThrow(A4pErrorCode.A4P_DESERIALIZE_FAIL,
        'Failed to deserialize IntentExecutionContext: ' + (e as Error).message);
    }
    const validationError = validateIntentExecutionContext(obj);
    if (validationError) {
      a4pThrow(validationError.code, validationError.detail);
    }
    obj.authorizedIntent = deepFreeze(obj.authorizedIntent);
    return obj;
  }
}
