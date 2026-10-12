/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

import { describe, it, expect } from '@ohos/hypium';
import { A4pCodec } from '../../../main/ets/api/codec';
import {
  A4pErrorCode, A4pError,
  IntentL1, IntentL2, IntentL3, PaymentConstraints,
  A4p, IntentExecutionContext, ExecutionStatus
} from '../../../../Index';
import {
  createTestL1, createTestL2, createTestL3, createTestCreateIntentRequest, createTestExecution
} from './mock/TestDataFactory';
import { MockVerifier } from './mock/MockSigner';

// Class instance used to verify canonicalStringify rejects non-plain objects.
class CustomInfo {
  /** Arbitrary payload field */
  field: string = 'value';
}

function createL1(): IntentL1 {
  return createTestL1();
}

function createL2(): IntentL2 {
  return createTestL2();
}

function createL3(): IntentL3 {
  return createTestL3();
}

function createExecutionContext(): IntentExecutionContext {
  return A4p.activateIntent(createTestL3(), new MockVerifier());
}

/**
 * A context whose ledger covers every record shape: one COMPLETED, one
 * CANCELED (both with their finalization timestamps), on periodic test
 * constraints (so records carry period indexes).
 */
function createExecutionContextWithLedger(): IntentExecutionContext {
  const context = createExecutionContext();
  const first = createTestExecution();
  A4p.createExecution(context, first);
  const second = createTestExecution();
  A4p.createExecution(context, second);
  A4p.completeExecution(context, first.id);
  A4p.cancelExecution(context, second.id);
  return context;
}

/** A context whose constraints omit the period rule (non-periodic). */
function createNonPeriodicExecutionContext(): IntentExecutionContext {
  const request = createTestCreateIntentRequest();
  request.structuredIntent.constraints = {
    totalMaxAmount: 500000,
    currency: 'CNY',
    validFrom: Date.now() - 60000,
    validUntil: Date.now() + 3600000
  };
  return A4p.activateIntent(createTestL3(createTestL2(createTestL1(request))), new MockVerifier());
}

function withoutKey(obj: Record<string, Object>, key: string): Record<string, Object> {
  const result: Record<string, Object> = {};
  const keys: string[] = Object.keys(obj);
  for (const k of keys) {
    if (k !== key) {
      result[k] = obj[k];
    }
  }
  return result;
}

function expectThrows(code: A4pErrorCode, fn: () => void): void {
  try {
    fn();
  } catch (e) {
    if (e instanceof A4pError) {
      expect((e as A4pError).code).assertEqual(code);
      return;
    }
    throw new Error('Expected A4pError with code ' + code + ', got: ' + (e as Error));
  }
  throw new Error('Expected A4pError with code ' + code + ', but no error was thrown');
}

export default function a4pCodecTest() {
  describe('A4pCodecTest', () => {

    describe('IntentL1', () => {
      it('serialize_success', 0, () => {
        const l1 = createL1();

        const json = A4pCodec.serializeIntentL1(l1);

        expect(json.length).assertLarger(0);
      });

      it('serialize_deterministic', 0, () => {
        const l1 = createL1();

        const r1 = A4pCodec.serializeIntentL1(l1);
        const r2 = A4pCodec.serializeIntentL1(l1);

        expect(r2).assertEqual(r1);
      });

      it('serialize_independentOfKeyInsertionOrder', 0, () => {
        const l1 = createL1();
        const r1 = A4pCodec.serializeIntentL1(l1);

        // Rebuild the same intent with every nested object's keys
        // assigned in reverse order; canonical output must be identical.
        const reordered: IntentL1 = {
          timestamp: l1.timestamp,
          structuredIntent: {
            constraints: l1.structuredIntent.constraints,
            resource: l1.structuredIntent.resource,
            action: l1.structuredIntent.action
          },
          intentSummary: l1.intentSummary,
          intentConversation: l1.intentConversation,
          intentId: l1.intentId,
          version: l1.version
        }
        reordered.sign = l1.sign;
        const r2 = A4pCodec.serializeIntentL1(reordered);

        expect(r2).assertEqual(r1);
      });

      it('serialize_includesAlgAndKeyIdentifier', 0, () => {
        const l1 = createL1();
        const json = A4pCodec.serializeIntentL1(l1);

        // alg/keyIdentifier live in the detached sign object; the wire
        // form must carry them so verifiers know what to verify with —
        // they are not part of the signable bytes.
        expect(json.indexOf('"alg"')).assertLarger(-1);
        expect(json.indexOf('"keyIdentifier"')).assertLarger(-1);
      });

      it('deserialize_success', 0, () => {
        const l1 = createL1();
        const serialized = A4pCodec.serializeIntentL1(l1);

        const result = A4pCodec.deserializeIntentL1(serialized);

        expect(result.intentId).assertEqual(l1.intentId);
        expect(result.sign?.sign).assertEqual(l1.sign?.sign);
      });

      it('deserialize_fails_whenEmpty', 0, () => {
        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4pCodec.deserializeIntentL1('');
        });
      });

      it('deserialize_fails_whenBodyIsNull', 0, () => {
        // JSON "null" parses to null; the null root must surface as a
        // validation failure, not a raw TypeError from dereferencing it.
        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4pCodec.deserializeIntentL1('null');
        });
      });

      it('deserialize_fails_whenInvalidJson', 0, () => {
        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          A4pCodec.deserializeIntentL1('not json');
        });
      });

      it('deserialize_fails_whenMissingVersion', 0, () => {
        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4pCodec.deserializeIntentL1('{"intentId":"test"}');
        });
      });

      it('deserialize_fails_whenMissingIntentId', 0, () => {
        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4pCodec.deserializeIntentL1('{"version":"1.0"}');
        });
      });

      it('deserialize_fails_whenMissingTimestamp', 0, () => {
        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4pCodec.deserializeIntentL1('{"version":"1.0","intentId":"test"}');
        });
      });

      it('deserialize_fails_whenMissingIntentConversation', 0, () => {
        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4pCodec.deserializeIntentL1('{"version":"1.0","intentId":"test","timestamp":1}');
        });
      });

      it('deserialize_fails_whenTimestampIsNegative', 0, () => {
        const l1 = createL1();
        const serialized = A4pCodec.serializeIntentL1(l1);
        const obj: Record<string, Object> = JSON.parse(serialized);
        obj['timestamp'] = -1;

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4pCodec.deserializeIntentL1(JSON.stringify(obj));
        });
      });

      it('deserialize_fails_withoutSign', 0, () => {
        // A signed intent is invalid without its signature: an unsigned
        // payload must not pass deserialization validation.
        const l1 = createL1();
        const serialized = A4pCodec.serializeIntentL1(l1);
        const obj: Record<string, Object> = JSON.parse(serialized);

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4pCodec.deserializeIntentL1(JSON.stringify(withoutKey(obj, 'sign')));
        });
      });

      it('roundTrip', 0, () => {
        const original = createL1();
        const serialized = A4pCodec.serializeIntentL1(original);
        const deserialized = A4pCodec.deserializeIntentL1(serialized);

        expect(deserialized.intentId).assertEqual(original.intentId);
        expect(deserialized.version).assertEqual(original.version);
        expect(deserialized.sign?.sign).assertEqual(original.sign?.sign);
      });
    });

    describe('IntentL2', () => {
      it('serialize_success', 0, () => {
        const l2 = createL2();

        const json = A4pCodec.serializeIntentL2(l2);

        expect(json.length).assertLarger(0);
      });

      it('serialize_deterministic', 0, () => {
        const l2 = createL2();

        const r1 = A4pCodec.serializeIntentL2(l2);
        const r2 = A4pCodec.serializeIntentL2(l2);

        expect(r2).assertEqual(r1);
      });

      it('serialize_keepsNestedIntentL1Sign', 0, () => {
        const l2 = createL2();

        const json = A4pCodec.serializeIntentL2(l2);

        // Serialization keeps all fields, including nested signatures;
        // only toSignableBytes (signing) strips the top-level sign.
        expect(json.indexOf('"' + l2.intentL1.sign?.sign + '"')).assertLarger(0);
        expect(json.indexOf('"' + l2.sign?.sign + '"')).assertLarger(0);
      });

      it('serialize_fails_whenNestedValueIsClassInstance', 0, () => {
        const l2 = createL2();
        const tainted: IntentL2 = {
          version: l2.version,
          intentL1: {
            version: l2.intentL1.version,
            intentId: l2.intentL1.intentId,
            intentConversation: l2.intentL1.intentConversation,
            intentSummary: l2.intentL1.intentSummary,
            structuredIntent: {
              action: l2.intentL1.structuredIntent.action,
              resource: l2.intentL1.structuredIntent.resource,
              constraints: new CustomInfo() as Object as PaymentConstraints
            },
            timestamp: l2.intentL1.timestamp,
            sign: l2.intentL1.sign
          },
          userIdentity: l2.userIdentity,
          agentIdentity: l2.agentIdentity,
          serviceProviderIdentity: l2.serviceProviderIdentity,
          timestamp: l2.timestamp
        }
        tainted.sign = l2.sign;

        expectThrows(A4pErrorCode.A4P_SERIALIZE_FAIL, () => {
          A4pCodec.serializeIntentL2(tainted);
        });
      });

      it('deserialize_success', 0, () => {
        const l2 = createL2();
        const serialized = A4pCodec.serializeIntentL2(l2);

        const result = A4pCodec.deserializeIntentL2(serialized);

        expect(result.intentL1.intentId).assertEqual(l2.intentL1.intentId);
        expect(result.userIdentity.id).assertEqual(l2.userIdentity.id);
      });

      it('deserialize_fails_whenEmpty', 0, () => {
        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4pCodec.deserializeIntentL2('');
        });
      });

      it('deserialize_fails_whenBodyIsNull', 0, () => {
        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4pCodec.deserializeIntentL2('null');
        });
      });

      it('deserialize_fails_whenInvalidJson', 0, () => {
        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          A4pCodec.deserializeIntentL2('{invalid}');
        });
      });

      it('deserialize_fails_whenMissingIntentL1', 0, () => {
        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4pCodec.deserializeIntentL2('{"version":"1.0"}');
        });
      });

      it('deserialize_fails_whenMissingUserIdentity', 0, () => {
        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4pCodec.deserializeIntentL2('{"version":"1.0","intentL1":{}}');
        });
      });

      it('deserialize_fails_whenMissingAgentIdentity', 0, () => {
        const l2 = createL2();
        const serialized = A4pCodec.serializeIntentL2(l2);
        const obj: Record<string, Object> = JSON.parse(serialized);

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4pCodec.deserializeIntentL2(JSON.stringify(withoutKey(obj, 'agentIdentity')));
        });
      });

      it('deserialize_fails_whenMissingServiceProviderIdentity', 0, () => {
        const l2 = createL2();
        const serialized = A4pCodec.serializeIntentL2(l2);
        const obj: Record<string, Object> = JSON.parse(serialized);

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4pCodec.deserializeIntentL2(JSON.stringify(withoutKey(obj, 'serviceProviderIdentity')));
        });
      });

      it('deserialize_fails_withoutSign', 0, () => {
        const l2 = createL2();
        const serialized = A4pCodec.serializeIntentL2(l2);
        const obj: Record<string, Object> = JSON.parse(serialized);

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4pCodec.deserializeIntentL2(JSON.stringify(withoutKey(obj, 'sign')));
        });
      });

      it('roundTrip', 0, () => {
        const original = createL2();
        const serialized = A4pCodec.serializeIntentL2(original);
        const deserialized = A4pCodec.deserializeIntentL2(serialized);

        expect(deserialized.intentL1.intentId).assertEqual(original.intentL1.intentId);
        expect(deserialized.userIdentity.id).assertEqual(original.userIdentity.id);
        expect(deserialized.sign?.sign).assertEqual(original.sign?.sign);
      });
    });

    describe('IntentL3', () => {
      it('serialize_success', 0, () => {
        const l3 = createL3();

        const json = A4pCodec.serializeIntentL3(l3);

        expect(json.length).assertLarger(0);
      });

      it('serialize_deterministic', 0, () => {
        const l3 = createL3();

        const r1 = A4pCodec.serializeIntentL3(l3);
        const r2 = A4pCodec.serializeIntentL3(l3);

        expect(r2).assertEqual(r1);
      });

      it('serialize_keepsNestedSignChain', 0, () => {
        const l3 = createL3();

        const json = A4pCodec.serializeIntentL3(l3);

        expect(json.indexOf('"' + l3.intentL2.sign?.sign + '"')).assertLarger(0);
        expect(json.indexOf('"' + l3.intentL2.intentL1.sign?.sign + '"'))
          .assertLarger(0);
      });

      it('deserialize_success', 0, () => {
        const l3 = createL3();
        const serialized = A4pCodec.serializeIntentL3(l3);

        const result = A4pCodec.deserializeIntentL3(serialized);

        expect(result.intentL2.intentL1.intentId).assertEqual(l3.intentL2.intentL1.intentId);
        expect(result.signingEnv !== undefined).assertTrue();
      });

      it('deserialize_fails_whenEmpty', 0, () => {
        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4pCodec.deserializeIntentL3('');
        });
      });

      it('deserialize_fails_whenBodyIsNull', 0, () => {
        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4pCodec.deserializeIntentL3('null');
        });
      });

      it('deserialize_fails_whenInvalidJson', 0, () => {
        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          A4pCodec.deserializeIntentL3('{bad json}');
        });
      });

      it('deserialize_fails_whenMissingIntentL2', 0, () => {
        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4pCodec.deserializeIntentL3('{"version":"1.0"}');
        });
      });

      it('deserialize_fails_whenMissingSigningEnv', 0, () => {
        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4pCodec.deserializeIntentL3('{"version":"1.0","intentL2":{}}');
        });
      });

      it('deserialize_fails_whenTimestampIsNegative', 0, () => {
        const l3 = createL3();
        const serialized = A4pCodec.serializeIntentL3(l3);
        const obj: Record<string, Object> = JSON.parse(serialized);
        obj['timestamp'] = -1;

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4pCodec.deserializeIntentL3(JSON.stringify(obj));
        });
      });

      it('deserialize_fails_withoutSign', 0, () => {
        const l3 = createL3();
        const serialized = A4pCodec.serializeIntentL3(l3);
        const obj: Record<string, Object> = JSON.parse(serialized);

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4pCodec.deserializeIntentL3(JSON.stringify(withoutKey(obj, 'sign')));
        });
      });

      it('roundTrip', 0, () => {
        const original = createL3();
        const serialized = A4pCodec.serializeIntentL3(original);
        const deserialized = A4pCodec.deserializeIntentL3(serialized);

        expect(deserialized.intentL2.intentL1.intentId).assertEqual(original.intentL2.intentL1.intentId);
        expect(deserialized.sign?.sign).assertEqual(original.sign?.sign);
      });
    });

    describe('IntentExecutionContext', () => {
      it('serialize_success', 0, () => {
        const context = createExecutionContext();

        const json = A4pCodec.serializeIntentExecutionContext(context);

        expect(json.length).assertLarger(0);
      });

      it('serialize_deterministic', 0, () => {
        const context = createExecutionContext();

        const r1 = A4pCodec.serializeIntentExecutionContext(context);
        const r2 = A4pCodec.serializeIntentExecutionContext(context);

        expect(r2).assertEqual(r1);
      });

      it('deserialize_success', 0, () => {
        const context = createExecutionContextWithLedger();

        const result = A4pCodec.deserializeIntentExecutionContext(
          A4pCodec.serializeIntentExecutionContext(context));

        expect(result.intentId).assertEqual(context.intentId);
        expect(result.status).assertEqual(context.status);
        expect(result.executions.length).assertEqual(2);
        expect(result.executions[0].status).assertEqual(ExecutionStatus.COMPLETED);
        expect(result.executions[1].status).assertEqual(ExecutionStatus.CANCELED);
        expect(result.executionInfo.leftAmount).assertEqual(context.executionInfo.leftAmount);
      });

      it('deserialize_freezesAuthorizedIntentSnapshot', 0, () => {
        // The restored context preserves the activation invariant: the
        // authorization snapshot is deep-frozen (the ledger staying
        // mutable is proven by deserialize_restoresAWorkingContext).
        // Object.isFrozen is not in the ArkTS restricted stdlib, so the
        // freeze is asserted behaviorally: a frozen write either throws
        // (strict mode) or silently no-ops — both leave the snapshot
        // intact.
        const context = createExecutionContext();
        const originalIntentId: string = context.authorizedIntent.intentL2.intentL1.intentId;

        const result = A4pCodec.deserializeIntentExecutionContext(
          A4pCodec.serializeIntentExecutionContext(context));

        let snapshotTampered: boolean = false;
        try {
          result.authorizedIntent.intentL2.intentL1.intentId = 'tampered-id';
          snapshotTampered = result.authorizedIntent.intentL2.intentL1.intentId === 'tampered-id';
        } catch (e) {
          // The strict-mode outcome of writing to a frozen object; the
          // snapshot stayed intact either way.
          snapshotTampered = false;
        }
        expect(snapshotTampered).assertFalse();
        expect(result.authorizedIntent.intentL2.intentL1.intentId).assertEqual(originalIntentId);
      });

      it('deserialize_restoresAWorkingContext', 0, () => {
        const context = createExecutionContext();
        A4p.createExecution(context, createTestExecution());

        const restored = A4pCodec.deserializeIntentExecutionContext(
          A4pCodec.serializeIntentExecutionContext(context));
        A4p.createExecution(restored, createTestExecution());

        expect(restored.executions.length).assertEqual(2);
        // Two reserved orders of 5000 against the 500000 budget.
        expect(restored.executionInfo.leftAmount).assertEqual(490000);
      });

      it('deserialize_fails_whenEmpty', 0, () => {
        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4pCodec.deserializeIntentExecutionContext('');
        });
      });

      it('deserialize_fails_whenBodyIsNull', 0, () => {
        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4pCodec.deserializeIntentExecutionContext('null');
        });
      });

      it('deserialize_fails_whenInvalidJson', 0, () => {
        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          A4pCodec.deserializeIntentExecutionContext('{bad json}');
        });
      });

      it('deserialize_fails_whenMissingAuthorizedIntent', 0, () => {
        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4pCodec.deserializeIntentExecutionContext(
            '{"version":"1.0","intentId":"test","status":"ACTIVE"}');
        });
      });

      it('deserialize_fails_whenIntentIdMismatched', 0, () => {
        const context = createExecutionContext();
        const serialized = A4pCodec.serializeIntentExecutionContext(context);
        const obj: Record<string, Object> = JSON.parse(serialized);
        obj['intentId'] = 'mismatched-intent-id';

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4pCodec.deserializeIntentExecutionContext(JSON.stringify(obj));
        });
      });

      it('deserialize_fails_whenStatusIsForeign', 0, () => {
        const context = createExecutionContext();
        const serialized = A4pCodec.serializeIntentExecutionContext(context);
        const obj: Record<string, Object> = JSON.parse(serialized);
        obj['status'] = 'PAUSED';

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4pCodec.deserializeIntentExecutionContext(JSON.stringify(obj));
        });
      });

      it('deserialize_fails_whenExecutionsIsNotAnArray', 0, () => {
        const context = createExecutionContext();
        const serialized = A4pCodec.serializeIntentExecutionContext(context);
        const obj: Record<string, Object> = JSON.parse(serialized);
        // An object literal needs a Record-typed target in ArkTS; assign
        // through a typed local so the slot receives a non-array object.
        const notAnArray: Record<string, Object> = {};
        obj['executions'] = notAnArray;

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4pCodec.deserializeIntentExecutionContext(JSON.stringify(obj));
        });
      });

      it('deserialize_fails_whenRecordStatusIsForeign', 0, () => {
        const context = createExecutionContextWithLedger();
        const serialized = A4pCodec.serializeIntentExecutionContext(context);
        const obj: Record<string, Object> = JSON.parse(serialized);
        const executions: Record<string, Object>[] = obj['executions'] as Record<string, Object>[];
        executions[0]['status'] = 'PAUSED';

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4pCodec.deserializeIntentExecutionContext(JSON.stringify(obj));
        });
      });

      it('deserialize_fails_whenCompletedRecordLacksCompletedAt', 0, () => {
        const context = createExecutionContextWithLedger();
        const serialized = A4pCodec.serializeIntentExecutionContext(context);
        const obj: Record<string, Object> = JSON.parse(serialized);
        const executions: Record<string, Object>[] = obj['executions'] as Record<string, Object>[];
        executions[0] = withoutKey(executions[0], 'completedAt');

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4pCodec.deserializeIntentExecutionContext(JSON.stringify(obj));
        });
      });

      it('deserialize_fails_whenPendingRecordCarriesCompletedAt', 0, () => {
        const context = createExecutionContext();
        A4p.createExecution(context, createTestExecution());
        const serialized = A4pCodec.serializeIntentExecutionContext(context);
        const obj: Record<string, Object> = JSON.parse(serialized);
        const executions: Record<string, Object>[] = obj['executions'] as Record<string, Object>[];
        executions[0]['completedAt'] = 1;

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4pCodec.deserializeIntentExecutionContext(JSON.stringify(obj));
        });
      });

      it('deserialize_fails_whenPeriodicRecordLacksPeriodIndex', 0, () => {
        // The default test constraints are periodic, so every record must
        // carry its period index.
        const context = createExecutionContext();
        A4p.createExecution(context, createTestExecution());
        const serialized = A4pCodec.serializeIntentExecutionContext(context);
        const obj: Record<string, Object> = JSON.parse(serialized);
        const executions: Record<string, Object>[] = obj['executions'] as Record<string, Object>[];
        executions[0] = withoutKey(executions[0], 'periodIndex');

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4pCodec.deserializeIntentExecutionContext(JSON.stringify(obj));
        });
      });

      it('deserialize_fails_whenNonPeriodicRecordCarriesPeriodIndex', 0, () => {
        const context = createNonPeriodicExecutionContext();
        A4p.createExecution(context, createTestExecution());
        const serialized = A4pCodec.serializeIntentExecutionContext(context);
        const obj: Record<string, Object> = JSON.parse(serialized);
        const executions: Record<string, Object>[] = obj['executions'] as Record<string, Object>[];
        executions[0]['periodIndex'] = 0;

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4pCodec.deserializeIntentExecutionContext(JSON.stringify(obj));
        });
      });

      it('deserialize_fails_whenExecutionInfoMissing', 0, () => {
        const context = createExecutionContext();
        const serialized = A4pCodec.serializeIntentExecutionContext(context);
        const obj: Record<string, Object> = JSON.parse(serialized);

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4pCodec.deserializeIntentExecutionContext(JSON.stringify(withoutKey(obj, 'executionInfo')));
        });
      });

      it('deserialize_fails_whenPeriodicInfoLacksPeriodFigures', 0, () => {
        const context = createExecutionContext();
        const serialized = A4pCodec.serializeIntentExecutionContext(context);
        const obj: Record<string, Object> = JSON.parse(serialized);
        obj['executionInfo'] = withoutKey(obj['executionInfo'] as Record<string, Object>, 'periodIndex');

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4pCodec.deserializeIntentExecutionContext(JSON.stringify(obj));
        });
      });

      it('roundTrip', 0, () => {
        const original = createExecutionContextWithLedger();
        const serialized = A4pCodec.serializeIntentExecutionContext(original);
        const deserialized = A4pCodec.deserializeIntentExecutionContext(serialized);

        expect(deserialized.intentId).assertEqual(original.intentId);
        expect(deserialized.authorizedIntent.intentL2.intentL1.intentId)
          .assertEqual(original.authorizedIntent.intentL2.intentL1.intentId);
        expect(deserialized.executionInfo.leftAmount).assertEqual(original.executionInfo.leftAmount);
      });
    });
  });
}
