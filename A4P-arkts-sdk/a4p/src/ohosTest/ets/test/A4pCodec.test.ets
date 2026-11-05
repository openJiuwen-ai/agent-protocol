/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

import { describe, it, expect } from '@ohos/hypium';
import { A4pCodec } from '../../../main/ets/api/codec';
import {
  A4pErrorCode,
  IntentL1, IntentL2, IntentL3, PaymentConstraints,
  A4p, IntentExecutionContext, ExecutionStatus, IntentExecutionStatus, AlgorithmType
} from '../../../../Index';
import { A4pError } from '../../../main/ets/models/common';
import {
  createTestL1, createTestL2, createTestL3, createTestCreateIntentRequest, createTestExecution,
  createTestCreateIntentRequestAround, createTestIntentIssueContext, createTestSigningEnv
} from './mock/TestDataFactory';
import { MockVerifier, MockSigner } from './mock/MockSigner';

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

/**
 * A context whose ledger holds two PENDING reservations — no finalization
 * timestamps, both counting toward every quota — for the restore-content
 * tests that mutate a record's reservation instant.
 */
function createExecutionContextWithPendingLedger(): IntentExecutionContext {
  const context = createExecutionContext();
  A4p.createExecution(context, createTestExecution());
  A4p.createExecution(context, createTestExecution());
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

// Wire-form accessors for the restore-content tests: they reach nested
// fields of the serialized context (constraints, ledger records, orders)
// to mutate one property at a time, mirroring how a tampered persisted
// context would arrive at deserializeIntentExecutionContext.

function wireL3Of(obj: Record<string, Object>): Record<string, Object> {
  return obj['authorizedIntent'] as Record<string, Object>;
}

function wireConstraintsOf(obj: Record<string, Object>): Record<string, Object> {
  const l3: Record<string, Object> = wireL3Of(obj);
  const l2: Record<string, Object> = l3['intentL2'] as Record<string, Object>;
  const l1: Record<string, Object> = l2['intentL1'] as Record<string, Object>;
  const structured: Record<string, Object> = l1['structuredIntent'] as Record<string, Object>;
  return structured['constraints'] as Record<string, Object>;
}

function wireExecutionsOf(obj: Record<string, Object>): Record<string, Object>[] {
  return obj['executions'] as Record<string, Object>[];
}

function wireOrderOf(record: Record<string, Object>): Record<string, Object> {
  return record['executionContent'] as Record<string, Object>;
}

export default function a4pCodecTest() {
  describe('A4pCodecTest', () => {

    describe('IntentL1', () => {
      it('serialize_success', 0, () => {
        const l1 = createL1();

        const json = A4pCodec.serializeIntentL1(l1);

        expect(json.length).assertLarger(0);
      });

      it('serialize_fails_whenNull', 0, () => {
        // JSON.parse keeps the runtime value null despite the declared
        // type; it must fail as invalid data, not render "null".
        const nullIntent: IntentL1 = JSON.parse('null') as IntentL1;

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4pCodec.serializeIntentL1(nullIntent);
        });
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
            resources: l1.structuredIntent.resources,
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

      it('deserialize_fails_whenDuplicateKey', 0, () => {
        // JSON.parse silently keeps the last duplicate; the strict parser
        // rejects the document instead — the bytes a signer signed carry
        // exactly one of each key, so a duplicated key must never
        // deserialize as anything.
        const l1 = createL1();
        const serialized = A4pCodec.serializeIntentL1(l1);
        const duplicated = '{"intentId":"injected",' + serialized.substring(1);

        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          A4pCodec.deserializeIntentL1(duplicated);
        });
      });

      it('deserialize_fails_whenUnknownTopLevelField', 0, () => {
        // The closed field set rejects fields the protocol does not define.
        const l1 = createL1();
        const serialized = A4pCodec.serializeIntentL1(l1);
        const extended = '{"unknownField":1,' + serialized.substring(1);

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4pCodec.deserializeIntentL1(extended);
        });
      });

      it('deserialize_fails_whenUnknownNestedField', 0, () => {
        const l1 = createL1();
        const serialized = A4pCodec.serializeIntentL1(l1);
        const obj: Record<string, Object> = JSON.parse(serialized);
        const sign: Record<string, Object> = obj['sign'] as Record<string, Object>;
        sign['extra'] = 'value';

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4pCodec.deserializeIntentL1(JSON.stringify(obj));
        });
      });

      it('deserialize_fails_whenConstraintsCarryUnknownField', 0, () => {
        // A close-but-wrong constraint name must not slip through as an
        // ignored extra: unknown fields are rejected at every level.
        const l1 = createL1();
        const serialized = A4pCodec.serializeIntentL1(l1);
        const obj: Record<string, Object> = JSON.parse(serialized);
        const si: Record<string, Object> = obj['structuredIntent'] as Record<string, Object>;
        const constraints: Record<string, Object> = si['constraints'] as Record<string, Object>;
        constraints['maxAmount'] = 1;

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4pCodec.deserializeIntentL1(JSON.stringify(obj));
        });
      });

      it('deserialize_fails_whenNullLiteral', 0, () => {
        // An explicit null inside the payload is neither a value nor an
        // absence the protocol defines: absent optional fields are omitted,
        // never spelled null. (The bare document "null" is the one
        // exception — see deserialize_fails_whenBodyIsNull.)
        const l1 = createL1();
        const serialized = A4pCodec.serializeIntentL1(l1);
        const nulled = '{"intentSummary":null,' + serialized.substring(1);

        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          A4pCodec.deserializeIntentL1(nulled);
        });
      });

      it('deserialize_fails_whenTrailingContent', 0, () => {
        const l1 = createL1();
        const serialized = A4pCodec.serializeIntentL1(l1);

        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          A4pCodec.deserializeIntentL1(serialized + ' {}');
        });
      });

      it('deserialize_fails_whenNumberIsNotSafeInteger', 0, () => {
        // 9007199254740993 (2^53 + 1) silently rounds to 9007199254740992
        // when held as a JS number: the validators would see a value the
        // document never spelled, so the parser must reject it outright.
        // The literal is spliced into the raw text — routing it through a
        // JS number would round it before the parser ever saw it. A
        // fraction and an overflowing exponent are rejected the same way.
        const l1 = createL1();
        const serialized = A4pCodec.serializeIntentL1(l1);
        const unsafe: string =
          serialized.replace('"timestamp":' + l1.timestamp, '"timestamp":9007199254740993');

        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          A4pCodec.deserializeIntentL1(unsafe);
        });
        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          A4pCodec.deserializeIntentL1('{"timestamp":1.5,"intentId":"test"}');
        });
        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          A4pCodec.deserializeIntentL1('{"timestamp":1e999}');
        });
      });

      it('deserialize_fails_whenNumberIsNotPlainIntegerSpelling', 0, () => {
        // Fraction and exponent spellings are rejected even when they
        // evaluate (or round) to an integer: the parsed value must be
        // exactly what the bytes spelled.
        const l1 = createL1();
        const serialized = A4pCodec.serializeIntentL1(l1);
        const splice = (spelling: string): string =>
          serialized.replace('"timestamp":' + l1.timestamp, '"timestamp":' + spelling);

        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          A4pCodec.deserializeIntentL1(splice(l1.timestamp + '.0'));
        });
        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          A4pCodec.deserializeIntentL1(splice(l1.timestamp + 'e0'));
        });
        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          A4pCodec.deserializeIntentL1(splice('2.5e1'));
        });
        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          A4pCodec.deserializeIntentL1('{"timestamp":1e-325}');
        });
        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          A4pCodec.deserializeIntentL1('{"timestamp":1.0000000000000001}');
        });
      });

      it('deserialize_fails_whenProtoKeyPresent', 0, () => {
        // Assigning a "__proto__" key through bracket notation replaces
        // the target's prototype instead of creating an own property: the
        // entry would vanish from Object.keys (invisible to the closed
        // field set) while still leaking into property reads.
        const l1 = createL1();
        const serialized = A4pCodec.serializeIntentL1(l1);

        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          A4pCodec.deserializeIntentL1('{"__proto__":' + serialized + '}');
        });
        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          A4pCodec.deserializeIntentL1('{"__proto__":1,' + serialized.substring(1));
        });
      });

      it('deserialize_combinesEscapedSurrogatePair', 0, () => {
        // A supplementary code point spelled as an escaped surrogate pair
        // is one character: the two escapes must combine into it.
        const l1 = createL1();
        const serialized = A4pCodec.serializeIntentL1(l1);
        const escaped: string =
          serialized.replace('"intentId":"test-intent-001"', '"intentId":"\\ud83d\\ude00"');

        const deserialized = A4pCodec.deserializeIntentL1(escaped);

        expect(deserialized.intentId).assertEqual('😀');
      });

      it('deserialize_acceptsRawSurrogatePair', 0, () => {
        // The same code point spelled with raw UTF-16 characters — what
        // any non-ASCII document carries.
        const l1 = createL1();
        const serialized = A4pCodec.serializeIntentL1(l1);
        const raw: string =
          serialized.replace('"intentId":"test-intent-001"', '"intentId":"😀"');

        const deserialized = A4pCodec.deserializeIntentL1(raw);

        expect(deserialized.intentId).assertEqual('😀');
      });

      it('deserialize_fails_whenSurrogateUnpaired', 0, () => {
        // A lone surrogate half (escaped or raw) builds a string no strict
        // UTF-8 encoder accepts: rejected at parse time, not at signing.
        const l1 = createL1();
        const serialized = A4pCodec.serializeIntentL1(l1);
        const withId = (value: string): string =>
          serialized.replace('"intentId":"test-intent-001"', '"intentId":"' + value + '"');

        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          A4pCodec.deserializeIntentL1(withId('\\ud800'));
        });
        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          A4pCodec.deserializeIntentL1(withId('\\udc00'));
        });
        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          A4pCodec.deserializeIntentL1(withId('\\ud800\\n'));
        });
        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          A4pCodec.deserializeIntentL1(withId('\\ud800\\ud800'));
        });
        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          A4pCodec.deserializeIntentL1(withId('\ud800'));
        });
      });

      it('deserialize_readsNegativeZeroAsZero', 0, () => {
        // "-0" reads as 0: the canonical form spells both zeros "0".
        // === cannot tell the two zeros apart (the first assertion passes
        // for -0 too); the reciprocal can — 1/0 is positive, 1/-0 is not.
        const l1 = createL1();
        const serialized = A4pCodec.serializeIntentL1(l1);
        const negativeZero: string =
          serialized.replace('"timestamp":' + l1.timestamp, '"timestamp":-0');

        const deserialized = A4pCodec.deserializeIntentL1(negativeZero);

        expect(deserialized.timestamp).assertEqual(0);
        expect(1 / deserialized.timestamp).assertLarger(0);
      });
    });

    describe('IntentL2', () => {
      it('serialize_success', 0, () => {
        const l2 = createL2();

        const json = A4pCodec.serializeIntentL2(l2);

        expect(json.length).assertLarger(0);
      });

      it('serialize_fails_whenNull', 0, () => {
        // JSON.parse keeps the runtime value null despite the declared
        // type; it must fail as invalid data, not render "null".
        const nullIntent: IntentL2 = JSON.parse('null') as IntentL2;

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4pCodec.serializeIntentL2(nullIntent);
        });
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
              resources: l2.intentL1.structuredIntent.resources,
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

      it('roundTripsJsonTextInStringFields', 0, () => {
        // A string field may itself carry JSON text (UserIdentity.
        // additionalInfo is documented as a JSON string): a null inside
        // that text is data, not a protocol null. Only a null literal at
        // a value position is rejected (see
        // deserialize_fails_whenNullLiteral); string contents are opaque
        // and round-trip verbatim, inner quotes and all.
        const original = createL2();
        original.userIdentity.additionalInfo = '{"tag":null,"note":"a \\"quoted\\" value"}';

        const serialized = A4pCodec.serializeIntentL2(original);
        const deserialized = A4pCodec.deserializeIntentL2(serialized);

        expect(deserialized.userIdentity.additionalInfo).assertEqual(original.userIdentity.additionalInfo);
      });
    });

    describe('IntentL3', () => {
      it('serialize_success', 0, () => {
        const l3 = createL3();

        const json = A4pCodec.serializeIntentL3(l3);

        expect(json.length).assertLarger(0);
      });

      it('serialize_fails_whenNull', 0, () => {
        // JSON.parse keeps the runtime value null despite the declared
        // type; it must fail as invalid data, not render "null".
        const nullIntent: IntentL3 = JSON.parse('null') as IntentL3;

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4pCodec.serializeIntentL3(nullIntent);
        });
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

      it('serialize_fails_whenNull', 0, () => {
        // JSON.parse keeps the runtime value null despite the declared
        // type; it must fail as invalid data, not render "null".
        const nullContext: IntentExecutionContext = JSON.parse('null') as IntentExecutionContext;

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4pCodec.serializeIntentExecutionContext(nullContext);
        });
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
        expect(result.activatedTime).assertEqual(context.activatedTime);
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

      it('deserialize_fails_whenActivatedTimeMissing', 0, () => {
        const context = createExecutionContext();
        const serialized = A4pCodec.serializeIntentExecutionContext(context);
        const obj: Record<string, Object> = JSON.parse(serialized);

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4pCodec.deserializeIntentExecutionContext(
            JSON.stringify(withoutKey(obj, 'activatedTime')));
        });
      });

      it('deserialize_roundTripsRevokedContext', 0, () => {
        const context = createExecutionContext();
        A4p.revokeIntent(context, context.activatedTime + 1000);

        const restored = A4pCodec.deserializeIntentExecutionContext(
          A4pCodec.serializeIntentExecutionContext(context));

        expect(restored.status).assertEqual(IntentExecutionStatus.REVOKED);
        expect(restored.revokedTime).assertEqual(context.activatedTime + 1000);
      });

      it('deserialize_fails_whenRevokedTimeMissingOnRevokedContext', 0, () => {
        // revokedTime is status-bound like the records' finalization
        // stamps: a REVOKED context without it is corrupted data.
        const context = createExecutionContext();
        A4p.revokeIntent(context, context.activatedTime + 1000);
        const serialized = A4pCodec.serializeIntentExecutionContext(context);
        const obj: Record<string, Object> = JSON.parse(serialized);

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4pCodec.deserializeIntentExecutionContext(
            JSON.stringify(withoutKey(obj, 'revokedTime')));
        });
      });

      it('deserialize_fails_whenRevokedTimeOnActiveContext', 0, () => {
        const context = createExecutionContext();
        const serialized = A4pCodec.serializeIntentExecutionContext(context);
        const obj: Record<string, Object> = JSON.parse(serialized);
        obj['revokedTime'] = (obj['activatedTime'] as number) + 1000;

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4pCodec.deserializeIntentExecutionContext(JSON.stringify(obj));
        });
      });

      it('deserialize_fails_whenRevokedTimeBeforeActivatedTime', 0, () => {
        // A revocation can never predate the activation it revokes; the
        // violation fails with the code revokeIntent would have returned.
        const context = createExecutionContext();
        A4p.revokeIntent(context, context.activatedTime + 1000);
        const serialized = A4pCodec.serializeIntentExecutionContext(context);
        const obj: Record<string, Object> = JSON.parse(serialized);
        obj['revokedTime'] = (obj['activatedTime'] as number) - 1;

        expectThrows(A4pErrorCode.A4P_TIMESTAMP_TOO_EARLY, () => {
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

      it('deserialize_fails_whenCompletedAtBeforeReservedAt', 0, () => {
        // completeExecution/cancelExecution refuse a clock earlier than the
        // record's reservedAt, so a persisted record violating that
        // monotonic rule is corrupted data: restore fails with the code the
        // offending operation would have returned.
        const context = createExecutionContextWithLedger();
        const serialized = A4pCodec.serializeIntentExecutionContext(context);
        const obj: Record<string, Object> = JSON.parse(serialized);
        const executions: Record<string, Object>[] = obj['executions'] as Record<string, Object>[];
        const reservedAt: number = executions[0]['reservedAt'] as number;
        executions[0]['completedAt'] = reservedAt - 1;

        expectThrows(A4pErrorCode.A4P_TIMESTAMP_TOO_EARLY, () => {
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

      it('deserialize_fails_whenRecordCarriesUnknownField', 0, () => {
        const context = createExecutionContext();
        A4p.createExecution(context, createTestExecution());
        const serialized = A4pCodec.serializeIntentExecutionContext(context);
        const obj: Record<string, Object> = JSON.parse(serialized);
        const executions: Record<string, Object>[] = obj['executions'] as Record<string, Object>[];
        executions[0]['note'] = 'extra';

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

    // Restore-content validation: beyond the structural pass above, the
    // deserializer replays the reachability rules the lifecycle APIs
    // enforce (see validateRestoredContext), so a tampered persisted
    // context fails with the code the offending operation would have
    // returned. Each test mutates exactly one wire property.
    describe('IntentExecutionContextRestoreContent', () => {
      it('restore_fails_whenAuthorizedChainVersionUnsupported', 0, () => {
        const serialized = A4pCodec.serializeIntentExecutionContext(createExecutionContext());
        const obj: Record<string, Object> = JSON.parse(serialized);
        wireL3Of(obj)['version'] = '0.9';

        expectThrows(A4pErrorCode.A4P_UNSUPPORTED_VERSION, () => {
          A4pCodec.deserializeIntentExecutionContext(JSON.stringify(obj));
        });
      });

      it('restore_fails_whenContextVersionUnsupported', 0, () => {
        // The shape pass only checks the version non-blank; the restore
        // pass must reject a context from a different protocol
        // generation, as the Java codec's restore does.
        const serialized = A4pCodec.serializeIntentExecutionContext(createExecutionContext());
        const obj: Record<string, Object> = JSON.parse(serialized);
        obj['version'] = '9.9';

        expectThrows(A4pErrorCode.A4P_UNSUPPORTED_VERSION, () => {
          A4pCodec.deserializeIntentExecutionContext(JSON.stringify(obj));
        });
      });

      it('restore_fails_whenActivatedTimeAfterValidUntil', 0, () => {
        const serialized = A4pCodec.serializeIntentExecutionContext(createExecutionContext());
        const obj: Record<string, Object> = JSON.parse(serialized);
        const validUntil: number = wireConstraintsOf(obj)['validUntil'] as number;
        obj['activatedTime'] = validUntil + 1;

        expectThrows(A4pErrorCode.A4P_TIMESTAMP_EXPIRED, () => {
          A4pCodec.deserializeIntentExecutionContext(JSON.stringify(obj));
        });
      });

      it('restore_succeeds_whenActivatedTimeIsExactlyValidUntil', 0, () => {
        // activateIntent admits now == validUntil; a context activated at
        // that boundary must keep round-tripping instead of expiring.
        const original = createExecutionContext();
        const serialized = A4pCodec.serializeIntentExecutionContext(original);
        const obj: Record<string, Object> = JSON.parse(serialized);
        const validUntil: number = wireConstraintsOf(obj)['validUntil'] as number;
        obj['activatedTime'] = validUntil;

        const restored = A4pCodec.deserializeIntentExecutionContext(JSON.stringify(obj));

        expect(restored.activatedTime).assertEqual(validUntil);
        expect(restored.intentId).assertEqual(original.intentId);
      });

      it('restore_fails_whenReservedAtBeforeActivatedTime', 0, () => {
        const serialized = A4pCodec.serializeIntentExecutionContext(createExecutionContextWithPendingLedger());
        const obj: Record<string, Object> = JSON.parse(serialized);
        wireExecutionsOf(obj)[0]['reservedAt'] = (obj['activatedTime'] as number) - 1;

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4pCodec.deserializeIntentExecutionContext(JSON.stringify(obj));
        });
      });

      it('restore_fails_whenReservedAtBeforeValidFrom', 0, () => {
        // Push validFrom past a record's reservation instant; activation
        // before validFrom is legal, so only the record's window check
        // fires (the activatedTime floor is satisfied).
        const serialized = A4pCodec.serializeIntentExecutionContext(createExecutionContextWithPendingLedger());
        const obj: Record<string, Object> = JSON.parse(serialized);
        const reservedAt: number = wireExecutionsOf(obj)[0]['reservedAt'] as number;
        wireConstraintsOf(obj)['validFrom'] = reservedAt + 1;

        expectThrows(A4pErrorCode.A4P_TIMESTAMP_TOO_EARLY, () => {
          A4pCodec.deserializeIntentExecutionContext(JSON.stringify(obj));
        });
      });

      it('restore_fails_whenReservedAtAfterValidUntil', 0, () => {
        const serialized = A4pCodec.serializeIntentExecutionContext(createExecutionContextWithPendingLedger());
        const obj: Record<string, Object> = JSON.parse(serialized);
        const validUntil: number = wireConstraintsOf(obj)['validUntil'] as number;
        wireExecutionsOf(obj)[0]['reservedAt'] = validUntil + 1;

        expectThrows(A4pErrorCode.A4P_TIMESTAMP_EXPIRED, () => {
          A4pCodec.deserializeIntentExecutionContext(JSON.stringify(obj));
        });
      });

      it('restore_fails_whenRecordPeriodIndexMismatched', 0, () => {
        const serialized = A4pCodec.serializeIntentExecutionContext(createExecutionContextWithLedger());
        const obj: Record<string, Object> = JSON.parse(serialized);
        wireExecutionsOf(obj)[0]['periodIndex'] = 1;

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4pCodec.deserializeIntentExecutionContext(JSON.stringify(obj));
        });
      });

      it('restore_fails_whenExecutionIdDuplicated', 0, () => {
        const serialized = A4pCodec.serializeIntentExecutionContext(createExecutionContextWithPendingLedger());
        const obj: Record<string, Object> = JSON.parse(serialized);
        const records: Record<string, Object>[] = wireExecutionsOf(obj);
        records[1]['executionId'] = records[0]['executionId'];

        expectThrows(A4pErrorCode.A4P_DUPLICATE_ID, () => {
          A4pCodec.deserializeIntentExecutionContext(JSON.stringify(obj));
        });
      });

      it('restore_fails_whenOrderIdDuplicated', 0, () => {
        const serialized = A4pCodec.serializeIntentExecutionContext(createExecutionContextWithPendingLedger());
        const obj: Record<string, Object> = JSON.parse(serialized);
        const records: Record<string, Object>[] = wireExecutionsOf(obj);
        wireOrderOf(records[1])['orderId'] = wireOrderOf(records[0])['orderId'];

        expectThrows(A4pErrorCode.A4P_DUPLICATE_ID, () => {
          A4pCodec.deserializeIntentExecutionContext(JSON.stringify(obj));
        });
      });

      it('restore_fails_whenRecordCurrencyMismatches', 0, () => {
        const serialized = A4pCodec.serializeIntentExecutionContext(createExecutionContextWithLedger());
        const obj: Record<string, Object> = JSON.parse(serialized);
        wireOrderOf(wireExecutionsOf(obj)[0])['currency'] = 'USD';

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4pCodec.deserializeIntentExecutionContext(JSON.stringify(obj));
        });
      });

      it('restore_reportsParameterErrorBeforeWindowError', 0, () => {
        // A currency mismatch reports 1001 even when the record also
        // violates validFrom, matching the Java ledger order; the
        // window-only case is covered by
        // restore_fails_whenReservedAtBeforeValidFrom.
        const serialized = A4pCodec.serializeIntentExecutionContext(createExecutionContextWithLedger());
        const obj: Record<string, Object> = JSON.parse(serialized);
        wireOrderOf(wireExecutionsOf(obj)[0])['currency'] = 'USD';
        const reservedAt: number = wireExecutionsOf(obj)[0]['reservedAt'] as number;
        wireConstraintsOf(obj)['validFrom'] = reservedAt + 1;

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4pCodec.deserializeIntentExecutionContext(JSON.stringify(obj));
        });
      });

      it('restore_fails_whenRecordAmountExceedsSingleOrderCap', 0, () => {
        const serialized = A4pCodec.serializeIntentExecutionContext(createExecutionContextWithLedger());
        const obj: Record<string, Object> = JSON.parse(serialized);
        wireOrderOf(wireExecutionsOf(obj)[0])['amount'] = 100001;

        expectThrows(A4pErrorCode.A4P_SINGLE_ORDER_AMOUNT_EXCEEDED, () => {
          A4pCodec.deserializeIntentExecutionContext(JSON.stringify(obj));
        });
      });

      it('restore_fails_whenCartQuantityMismatchesTemplate', 0, () => {
        const serialized = A4pCodec.serializeIntentExecutionContext(createExecutionContextWithLedger());
        const obj: Record<string, Object> = JSON.parse(serialized);
        const order: Record<string, Object> = wireOrderOf(wireExecutionsOf(obj)[0]);
        const cartItems: Record<string, Object>[] = order['cartItems'] as Record<string, Object>[];
        cartItems[0]['quantity'] = (cartItems[0]['quantity'] as number) + 1;

        expectThrows(A4pErrorCode.A4P_CART_MISMATCH, () => {
          A4pCodec.deserializeIntentExecutionContext(JSON.stringify(obj));
        });
      });

      it('restore_fails_whenLedgerExceedsTotalAmount', 0, () => {
        // The ledger holds one COMPLETED order of 5000 (the CANCELED one
        // is excluded); a totalMaxAmount below that is not a cap the
        // ledger could have been produced under.
        const serialized = A4pCodec.serializeIntentExecutionContext(createExecutionContextWithLedger());
        const obj: Record<string, Object> = JSON.parse(serialized);
        wireConstraintsOf(obj)['totalMaxAmount'] = 4999;

        expectThrows(A4pErrorCode.A4P_AMOUNT_EXCEEDED, () => {
          A4pCodec.deserializeIntentExecutionContext(JSON.stringify(obj));
        });
      });

      it('restore_fails_whenLedgerExceedsExecutionCount', 0, () => {
        const serialized = A4pCodec.serializeIntentExecutionContext(createExecutionContextWithPendingLedger());
        const obj: Record<string, Object> = JSON.parse(serialized);
        wireConstraintsOf(obj)['maxExecutionCount'] = 1;

        expectThrows(A4pErrorCode.A4P_EXECUTION_COUNT_EXCEEDED, () => {
          A4pCodec.deserializeIntentExecutionContext(JSON.stringify(obj));
        });
      });

      it('restore_fails_whenLedgerExceedsPeriodExecutionCount', 0, () => {
        const serialized = A4pCodec.serializeIntentExecutionContext(createExecutionContextWithPendingLedger());
        const obj: Record<string, Object> = JSON.parse(serialized);
        wireConstraintsOf(obj)['maxExecutionCountPerPeriod'] = 1;

        expectThrows(A4pErrorCode.A4P_PERIOD_EXECUTION_COUNT_EXCEEDED, () => {
          A4pCodec.deserializeIntentExecutionContext(JSON.stringify(obj));
        });
      });

      it('restore_fails_whenStatusContradictsLedger', 0, () => {
        // Two PENDING records leave the ledger unexhausted: a CONSUMED
        // status is not one completeExecution could have produced.
        const serialized = A4pCodec.serializeIntentExecutionContext(createExecutionContextWithPendingLedger());
        const obj: Record<string, Object> = JSON.parse(serialized);
        obj['status'] = 'CONSUMED';

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4pCodec.deserializeIntentExecutionContext(JSON.stringify(obj));
        });
      });

      it('restore_fails_whenLeftAmountDisagreesWithLedger', 0, () => {
        const serialized = A4pCodec.serializeIntentExecutionContext(createExecutionContextWithPendingLedger());
        const obj: Record<string, Object> = JSON.parse(serialized);
        const info: Record<string, Object> = obj['executionInfo'] as Record<string, Object>;
        info['leftAmount'] = (info['leftAmount'] as number) - 1;

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4pCodec.deserializeIntentExecutionContext(JSON.stringify(obj));
        });
      });

      it('restore_fails_whenLeftPeriodCountDisagreesWithLedger', 0, () => {
        const serialized = A4pCodec.serializeIntentExecutionContext(createExecutionContextWithPendingLedger());
        const obj: Record<string, Object> = JSON.parse(serialized);
        const info: Record<string, Object> = obj['executionInfo'] as Record<string, Object>;
        info['leftPeriodExecutionCount'] = (info['leftPeriodExecutionCount'] as number) - 1;

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4pCodec.deserializeIntentExecutionContext(JSON.stringify(obj));
        });
      });
    });

    describe('GoldenWireKeys', () => {
      it('serialize_goldenWireKeys_fixedClock', 0, () => {
        // Golden wire-format vectors on a fixed clock: every wire-visible
        // property name is asserted verbatim against the canonical JSON.
        // This is the obfuscation tripwire — round-trip tests cannot
        // catch property renaming (renamed keys round-trip consistently),
        // while these literal key assertions fail the moment a missing
        // -keep-property-name (consumer-rules.txt) lets
        // -enable-property-obfuscation rename a protocol field. Run this
        // suite once against a release build with obfuscation enabled to
        // verify the shipped artifact.
        const now: number = 1700000000000;
        const request = createTestCreateIntentRequestAround(now);
        const l1: IntentL1 = A4p.createIntent(request, new MockSigner(), now);
        const l2: IntentL2 = A4p.issueIntent(l1, new MockVerifier(), createTestIntentIssueContext(),
          new MockSigner(AlgorithmType.RSA3072, 'server-key-001'), now + 1000);
        const l3: IntentL3 = A4p.signIntent(l2, new MockVerifier(), createTestSigningEnv(),
          new MockSigner(AlgorithmType.RSA3072, 'user-key-001'), now + 2000);

        const j1: string = A4pCodec.serializeIntentL1(l1);
        const j2: string = A4pCodec.serializeIntentL2(l2);
        const j3: string = A4pCodec.serializeIntentL3(l3);

        // L1 identity and summary fields, plus the detached sign metadata.
        expect(j1).assertContain('"version":"1.0"');
        expect(j1).assertContain('"intentId":"test-intent-001"');
        expect(j1).assertContain('"intentConversation":"agent_conversation"');
        expect(j1).assertContain('"intentSummary":"Test payment intent"');
        expect(j1).assertContain('"timestamp":1700000000000');
        expect(j1).assertContain('"alg":"RSA3072"');
        expect(j1).assertContain('"keyIdentifier":"key-001"');
        expect(j1).assertContain('"sign":"');

        // L1 structuredIntent: action, order template, constraints. The
        // resources key is asserted both positively and negatively: the
        // wire name must never drift back to the old singular form this
        // SDK once signed.
        expect(j1).assertContain('"action":"AI_PAY"');
        expect(j1).assertContain('"resources":{');
        expect(j1).assertContain('"itemsWhiteList":[');
        expect(j1).assertContain('"whiteList":[');
        expect(j1).assertContain('"quantity":1');
        expect(j1).assertContain('"seller":{"id":"seller-001"}');
        expect(j1).assertContain('"spusku":{"skuId":"sku-001","spuId":"spu-001"}');
        expect(j1).assertContain('"singleOrderMaxAmount":100000');
        expect(j1).assertContain('"totalMaxAmount":500000');
        expect(j1).assertContain('"currency":"CNY"');
        expect(j1).assertContain('"validFrom":1699999940000');
        expect(j1).assertContain('"validUntil":1700003600000');
        expect(j1).assertContain('"maxExecutionCount":10');
        expect(j1).assertContain('"anchorTime":1699999940000');
        expect(j1).assertContain('"periodSeconds":3600');
        expect(j1).assertContain('"maxExecutionCountPerPeriod":10');
        expect(j1.indexOf('"resource":')).assertEqual(-1);

        // L2 embeds the full L1 (its signature included) and adds the
        // three identities plus the server's sign metadata.
        expect(j2).assertContain('"intentL1":{');
        expect(j2).assertContain(
          '"userIdentity":{"additionalInfo":"{}","id":"user-001","identityProvider":"test-provider"}');
        expect(j2).assertContain(
          '"agentIdentity":{"additionalInfo":"{}","id":"agent-001","identityProvider":"test-provider"}');
        expect(j2).assertContain('"serviceProviderIdentity":{"additionalInfo":"{}","id":"sp-001"}');
        expect(j2).assertContain('"keyIdentifier":"server-key-001"');
        expect(j2).assertContain('"timestamp":1700000001000');

        // L3 embeds the full L2 and adds the signing environment (its
        // free-form device/app keys are part of the signed bytes too).
        expect(j3).assertContain('"intentL2":{');
        expect(j3).assertContain(
          '"signingEnv":{"app":{"name":"TestApp","version":"1.0.0"},"device":{"model":"TestDevice","os":"HarmonyOS"}}');
        expect(j3).assertContain('"keyIdentifier":"user-key-001"');
        expect(j3).assertContain('"timestamp":1700000002000');

        // The execution context: its wire keys (activatedTime included —
        // the activation instant is serialized with the context) are part
        // of the context codec's format.
        const ctx: IntentExecutionContext = A4p.activateIntent(l3, new MockVerifier(), now + 3000);
        const jc: string = A4pCodec.serializeIntentExecutionContext(ctx);
        expect(jc).assertContain('"version":"1.0"');
        expect(jc).assertContain('"intentId":"test-intent-001"');
        expect(jc).assertContain('"authorizedIntent":{');
        expect(jc).assertContain('"status":"ACTIVE"');
        expect(jc).assertContain('"activatedTime":1700000003000');
        expect(jc).assertContain('"executions":[]');
        expect(jc).assertContain('"executionInfo":{');
        expect(jc).assertContain('"periodIndex":0');
        expect(jc).assertContain('"leftPeriodExecutionCount":10');
        expect(jc).assertContain('"leftExecutionCount":10');
        expect(jc).assertContain('"leftAmount":500000');
        // revokedTime appears exactly when the status is REVOKED: absent on
        // the ACTIVE form, stamped on the wire by the revoked one.
        expect(jc.indexOf('"revokedTime"')).assertEqual(-1);
        const revoked: IntentExecutionContext = A4p.revokeIntent(ctx, now + 4000);
        const jr: string = A4pCodec.serializeIntentExecutionContext(revoked);
        expect(jr).assertContain('"status":"REVOKED"');
        expect(jr).assertContain('"revokedTime":1700000004000');
      });
    });
  });
}
