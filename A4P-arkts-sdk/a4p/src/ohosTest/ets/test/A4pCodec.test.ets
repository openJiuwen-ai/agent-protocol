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
  return A4p.activateIntent(createTestL3(), new MockVerifier(), Date.now());
}

/**
 * A context whose ledger covers every record shape: one COMPLETED, one
 * CANCELED (both with their finalization timestamps), on periodic test
 * constraints (so records carry period indexes).
 */
function createExecutionContextWithLedger(): IntentExecutionContext {
  const context = createExecutionContext();
  const first = createTestExecution();
  A4p.createExecution(context, first, Date.now());
  const second = createTestExecution();
  A4p.createExecution(context, second, Date.now());
  A4p.completeExecution(context, first.id, Date.now());
  A4p.cancelExecution(context, second.id, Date.now());
  return context;
}

/**
 * A context whose ledger holds two PENDING reservations — no finalization
 * timestamps, both counting toward every quota — for the restore tests
 * that mutate a record's reservation instant.
 */
function createExecutionContextWithPendingLedger(): IntentExecutionContext {
  const context = createExecutionContext();
  A4p.createExecution(context, createTestExecution(), Date.now());
  A4p.createExecution(context, createTestExecution(), Date.now());
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
  return A4p.activateIntent(createTestL3(createTestL2(createTestL1(request))), new MockVerifier(), Date.now());
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

/**
 * Rebuilds a plain object with its keys in canonical (sorted) order. The
 * restore tests mutate a wire document and re-stringify it; a key added
 * by bracket assignment lands at the end, which the parser's
 * canonical-order check would reject before the defect under test is
 * reached — resorting keeps the failure where the test aims it.
 */
function sortedKeysCopy(obj: Record<string, Object>): Record<string, Object> {
  const result: Record<string, Object> = {};
  const keys: string[] = Object.keys(obj).sort();
  for (const k of keys) {
    result[k] = obj[k];
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

/**
 * Builds the JSON text of `levels` nested single-key objects with the
 * scalar 1 at the bottom, for the nesting-limit tests: a value inside
 * the result sits `levels` containers deep.
 */
function nestedChainJson(levels: number): string {
  let chain: string = '1';
  for (let i = 0; i < levels; i++) {
    chain = '{"a":' + chain + '}';
  }
  return chain;
}

/**
 * Builds `levels` nested single-key objects with the scalar 1 at the
 * bottom, the object form of nestedChainJson for the serializer-side
 * nesting test.
 */
function nestedChainObject(levels: number): Record<string, Object> {
  let value: Object = 1;
  for (let i = 0; i < levels; i++) {
    const next: Record<string, Object> = { 'v': value };
    value = next as Object;
  }
  return value as Record<string, Object>;
}

// Wire-form accessors for the restore tests: they reach nested fields of
// the serialized context (constraints, ledger records, orders) to mutate
// one property at a time, mirroring how a tampered persisted context
// would arrive at deserializeIntentExecutionContext.

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

        expectThrows(A4pErrorCode.A4P_SERIALIZE_FAIL, () => {
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
        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          A4pCodec.deserializeIntentL1('');
        });
      });

      it('deserialize_fails_whenBodyIsNull', 0, () => {
        // JSON "null" parses to null; the null root must surface as a
        // deserialization failure, not a raw TypeError from dereferencing it.
        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          A4pCodec.deserializeIntentL1('null');
        });
      });

      it('deserialize_fails_whenInvalidJson', 0, () => {
        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          A4pCodec.deserializeIntentL1('not json');
        });
      });

      it('deserialize_fails_whenMissingVersion', 0, () => {
        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          A4pCodec.deserializeIntentL1('{"intentId":"test"}');
        });
      });

      it('deserialize_succeeds_whenVersionForeign', 0, () => {
        // Deserialization no longer judges the version: a level from
        // another protocol generation is restored verbatim — versions
        // are judged by the chain-facing APIs (issueIntent,
        // signIntent, activateIntent, verifyIntentAuthenticity), not
        // by the codec.
        const l1 = createL1();
        const serialized = A4pCodec.serializeIntentL1(l1);
        const obj: Record<string, Object> = JSON.parse(serialized);
        obj['version'] = '2.0';

        const restored = A4pCodec.deserializeIntentL1(JSON.stringify(obj));

        expect(restored.version).assertEqual('2.0');
        expect(restored.intentId).assertEqual(l1.intentId);
      });

      it('deserialize_fails_whenMissingIntentId', 0, () => {
        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          A4pCodec.deserializeIntentL1('{"version":"1.0"}');
        });
      });

      it('deserialize_fails_whenTimestampMissing', 0, () => {
        // timestamp is a required non-negative integer: a wire form
        // omitting it is invalid data.
        const l1 = createL1();
        const serialized = A4pCodec.serializeIntentL1(l1);
        const obj: Record<string, Object> = JSON.parse(serialized);

        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          A4pCodec.deserializeIntentL1(JSON.stringify(withoutKey(obj, 'timestamp')));
        });
      });

      it('deserialize_fails_whenMissingIntentConversation', 0, () => {
        // Keys in canonical order, so the missing field (not the parser's
        // order check) is what rejects the document.
        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          A4pCodec.deserializeIntentL1('{"intentId":"test","timestamp":1,"version":"1.0"}');
        });
      });

      it('deserialize_fails_whenTimestampIsNegative', 0, () => {
        // timestamp must be a non-negative integer.
        const l1 = createL1();
        const serialized = A4pCodec.serializeIntentL1(l1);
        const obj: Record<string, Object> = JSON.parse(serialized);
        obj['timestamp'] = -1;

        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          A4pCodec.deserializeIntentL1(JSON.stringify(obj));
        });
      });

      it('deserialize_fails_withoutSign', 0, () => {
        // A signed intent is invalid without its signature: an unsigned
        // payload must not pass deserialization validation.
        const l1 = createL1();
        const serialized = A4pCodec.serializeIntentL1(l1);
        const obj: Record<string, Object> = JSON.parse(serialized);

        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
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
        // Canonical in, canonical out: the restored object re-serializes
        // to the exact input bytes.
        expect(A4pCodec.serializeIntentL1(deserialized)).assertEqual(serialized);
      });

      it('deserialize_fails_whenDuplicateKey', 0, () => {
        // JSON.parse silently keeps the last duplicate; the strict parser
        // rejects the document instead — the bytes a signer signed carry
        // exactly one of each key, so a duplicated key must never
        // deserialize as anything. The duplicate is appended after the
        // final key so the failure comes from the duplicate check itself:
        // a repeated key placed anywhere before its canonical slot would
        // trip the canonical-order check first (see
        // deserialize_fails_whenKeysOutOfCanonicalOrder).
        const l1 = createL1();
        const serialized = A4pCodec.serializeIntentL1(l1);
        const parsed: Record<string, Object> = JSON.parse(serialized);
        const keys: string[] = Object.keys(parsed);
        const lastKey: string = keys[keys.length - 1];
        const duplicated = serialized.substring(0, serialized.length - 1) +
          ',"' + lastKey + '":"injected"}';

        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          A4pCodec.deserializeIntentL1(duplicated);
        });
      });

      it('deserialize_fails_whenKeysOutOfCanonicalOrder', 0, () => {
        // The canonical serializer emits every object's keys sorted by
        // UTF-16 code units, and the input must be those exact bytes: a
        // reordered but otherwise well-formed document — same pairs,
        // different order — is rejected at parse time instead of being
        // silently re-canonicalized. JSON.stringify emits the mutated
        // documents compactly, so key order is the only deviation from
        // the canonical form.
        const l1 = createL1();
        const serialized = A4pCodec.serializeIntentL1(l1);
        const obj: Record<string, Object> = JSON.parse(serialized);
        const keys: string[] = Object.keys(obj); // canonical input → sorted

        // Every top-level pair reversed.
        const reversed: Record<string, Object> = {};
        for (let i = keys.length - 1; i >= 0; i--) {
          reversed[keys[i]] = obj[keys[i]];
        }
        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          A4pCodec.deserializeIntentL1(JSON.stringify(reversed));
        });

        // A single adjacent transposition.
        const swapped: Record<string, Object> = {};
        for (let i = 0; i < keys.length; i++) {
          const k: string = i === 0 ? keys[1] : (i === 1 ? keys[0] : keys[i]);
          swapped[k] = obj[k];
        }
        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          A4pCodec.deserializeIntentL1(JSON.stringify(swapped));
        });

        // Top level canonical, one nested object reversed.
        const nested: Record<string, Object> = JSON.parse(serialized);
        const structured: Record<string, Object> =
          nested['structuredIntent'] as Record<string, Object>;
        const structuredKeys: string[] = Object.keys(structured);
        const structuredReversed: Record<string, Object> = {};
        for (let i = structuredKeys.length - 1; i >= 0; i--) {
          structuredReversed[structuredKeys[i]] = structured[structuredKeys[i]];
        }
        nested['structuredIntent'] = structuredReversed;
        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          A4pCodec.deserializeIntentL1(JSON.stringify(nested));
        });
      });

      it('deserialize_fails_whenUnknownTopLevelField', 0, () => {
        // The closed field set rejects fields the protocol does not
        // define. The injected key sorts before every L1 field, so the
        // canonical key order holds and the closed field set (not the
        // parser's order check) does the rejecting.
        const l1 = createL1();
        const serialized = A4pCodec.serializeIntentL1(l1);
        const extended = '{"extraField":1,' + serialized.substring(1);

        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          A4pCodec.deserializeIntentL1(extended);
        });
      });

      it('deserialize_fails_whenUnknownNestedField', 0, () => {
        const l1 = createL1();
        const serialized = A4pCodec.serializeIntentL1(l1);
        const obj: Record<string, Object> = JSON.parse(serialized);
        const sign: Record<string, Object> = obj['sign'] as Record<string, Object>;
        sign['extra'] = 'value';
        // Back into canonical order, so the order check stays quiet and
        // the closed field set rejects the extra field.
        obj['sign'] = sortedKeysCopy(sign);

        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          A4pCodec.deserializeIntentL1(JSON.stringify(obj));
        });
      });

      it('deserialize_fails_whenConstraintsCarryUnknownField', 0, () => {
        // A close-but-wrong constraint name must not slip through as an
        // ignored extra: unknown fields are rejected at every level. The
        // mutated constraints object is re-sorted so the order check
        // stays quiet and the closed field set does the rejecting.
        const l1 = createL1();
        const serialized = A4pCodec.serializeIntentL1(l1);
        const obj: Record<string, Object> = JSON.parse(serialized);
        const si: Record<string, Object> = obj['structuredIntent'] as Record<string, Object>;
        const constraints: Record<string, Object> = si['constraints'] as Record<string, Object>;
        constraints['maxAmount'] = 1;
        si['constraints'] = sortedKeysCopy(constraints);

        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
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

      it('deserialize_fails_whenWhitespacePresent', 0, () => {
        // The canonical form carries no whitespace: deserialization
        // accepts exactly the bytes the canonical serializer emits, so
        // padded or pretty-printed input is rejected rather than
        // silently re-canonicalized — leading, trailing and interior
        // alike (a persisted file gaining a trailing newline is refused,
        // not trimmed).
        const l1 = createL1();
        const serialized = A4pCodec.serializeIntentL1(l1);

        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          A4pCodec.deserializeIntentL1(' ' + serialized);
        });
        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          A4pCodec.deserializeIntentL1(serialized + '\n');
        });
        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          A4pCodec.deserializeIntentL1(serialized.replace('{', '{ '));
        });
        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          A4pCodec.deserializeIntentL1(serialized.replace(':', ': '));
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

      it('deserialize_fails_whenEscapeSpellingNonCanonical', 0, () => {
        // The canonical form spells each character exactly one way — \",
        // \\, \b, \f, \n, \r, \t, lowercase \u00xx for the remaining
        // control characters below 0x20, and every other character raw —
        // and the input must be those exact bytes, matching the Java
        // SDK, which accepts a document only when it equals its own
        // canonical re-serialization. Any other spelling of a character
        // is rejected rather than silently re-canonicalized.
        const l1 = createL1();
        const serialized = A4pCodec.serializeIntentL1(l1);
        const withId = (value: string): string =>
          serialized.replace('"intentId":"test-intent-001"', '"intentId":"' + value + '"');

        // The boundary of the rule: a canonical \u escape — a control
        // character with no short spelling, lowercase hex — parses.
        const canonical = A4pCodec.deserializeIntentL1(withId('a\\u0001b'));
        expect(canonical.intentId).assertEqual('a\u0001b');

        // The solidus escaped: the canonical form writes it raw.
        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          A4pCodec.deserializeIntentL1(withId('a\\/b'));
        });
        // An ordinary character spelled via \u: the canonical form
        // writes it raw.
        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          A4pCodec.deserializeIntentL1(withId('\\u0061bc'));
        });
        // A control character that has a short escape, spelled via \u:
        // the canonical form writes \b for 0x08.
        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          A4pCodec.deserializeIntentL1(withId('a\\u0008b'));
        });
        // Uppercase hex digits: the canonical form writes lowercase.
        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          A4pCodec.deserializeIntentL1(withId('a\\u001Fb'));
        });
        // An astral character spelled as an escaped surrogate pair: the
        // canonical form writes the code point raw, so the pair is a
        // non-canonical spelling, not two halves to combine.
        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          A4pCodec.deserializeIntentL1(withId('\\ud83d\\ude00'));
        });
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

      it('deserialize_fails_whenNegativeZero', 0, () => {
        // "-0" is not a canonical spelling: zero is spelled only "0"
        // (serialization renders -0 as 0).
        const l1 = createL1();
        const serialized = A4pCodec.serializeIntentL1(l1);
        const negativeZero: string =
          serialized.replace('"timestamp":' + l1.timestamp, '"timestamp":-0');

        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          A4pCodec.deserializeIntentL1(negativeZero);
        });
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

        expectThrows(A4pErrorCode.A4P_SERIALIZE_FAIL, () => {
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
        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          A4pCodec.deserializeIntentL2('');
        });
      });

      it('deserialize_fails_whenBodyIsNull', 0, () => {
        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          A4pCodec.deserializeIntentL2('null');
        });
      });

      it('deserialize_fails_whenInvalidJson', 0, () => {
        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          A4pCodec.deserializeIntentL2('{invalid}');
        });
      });

      it('deserialize_fails_whenMissingIntentL1', 0, () => {
        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          A4pCodec.deserializeIntentL2('{"version":"1.0"}');
        });
      });

      it('deserialize_succeeds_whenL2VersionForeign', 0, () => {
        // Deserialization no longer judges the version: the L2's own
        // version is restored verbatim.
        const l2 = createL2();
        const serialized = A4pCodec.serializeIntentL2(l2);
        const obj: Record<string, Object> = JSON.parse(serialized);
        obj['version'] = '2.0';

        const restored = A4pCodec.deserializeIntentL2(JSON.stringify(obj));

        expect(restored.version).assertEqual('2.0');
        expect(restored.intentL1.intentId).assertEqual(l2.intentL1.intentId);
      });

      it('deserialize_succeeds_whenEmbeddedL1VersionForeign', 0, () => {
        // The embedded L1's version is restored verbatim too: even a
        // mixed-version chain is data, and deserialization returns data —
        // it does not judge it.
        const l2 = createL2();
        const serialized = A4pCodec.serializeIntentL2(l2);
        const obj: Record<string, Object> = JSON.parse(serialized);
        const l1: Record<string, Object> = obj['intentL1'] as Record<string, Object>;
        l1['version'] = '2.0';

        const restored = A4pCodec.deserializeIntentL2(JSON.stringify(obj));

        expect(restored.version).assertEqual(l2.version);
        expect(restored.intentL1.version).assertEqual('2.0');
      });

      it('deserialize_fails_whenMissingUserIdentity', 0, () => {
        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          A4pCodec.deserializeIntentL2('{"intentL1":{},"version":"1.0"}');
        });
      });

      it('deserialize_fails_whenMissingAgentIdentity', 0, () => {
        const l2 = createL2();
        const serialized = A4pCodec.serializeIntentL2(l2);
        const obj: Record<string, Object> = JSON.parse(serialized);

        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          A4pCodec.deserializeIntentL2(JSON.stringify(withoutKey(obj, 'agentIdentity')));
        });
      });

      it('deserialize_fails_whenMissingServiceProviderIdentity', 0, () => {
        const l2 = createL2();
        const serialized = A4pCodec.serializeIntentL2(l2);
        const obj: Record<string, Object> = JSON.parse(serialized);

        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          A4pCodec.deserializeIntentL2(JSON.stringify(withoutKey(obj, 'serviceProviderIdentity')));
        });
      });

      it('deserialize_fails_withoutSign', 0, () => {
        const l2 = createL2();
        const serialized = A4pCodec.serializeIntentL2(l2);
        const obj: Record<string, Object> = JSON.parse(serialized);

        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
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

        expectThrows(A4pErrorCode.A4P_SERIALIZE_FAIL, () => {
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

      it('serialize_fails_whenNestingExceeds100Levels', 0, () => {
        // The 100-level nesting limit is shared with the strict parser
        // and the Java SDK (which enforces it on both directions): a
        // value may sit at most 100 containers deep. signingEnv.device is
        // a free-form object, so a deep chain there reaches the
        // serializer through a fully validated intent. The device chain
        // sits under two containers (root, signingEnv), so a chain of 98
        // leaves its scalar exactly 100 deep and a chain of 99 one past
        // the limit.
        const l3 = createL3();

        // At the limit the serialization succeeds — and its output is a
        // document the strict parser accepts back.
        l3.signingEnv.device = nestedChainObject(98);
        const boundary: string = A4pCodec.serializeIntentL3(l3);
        const restored = A4pCodec.deserializeIntentL3(boundary);
        expect(A4pCodec.serializeIntentL3(restored)).assertEqual(boundary);

        // One level deeper fails with the uniform serialization error.
        l3.signingEnv.device = nestedChainObject(99);
        expectThrows(A4pErrorCode.A4P_SERIALIZE_FAIL, () => {
          A4pCodec.serializeIntentL3(l3);
        });
      });

      it('deserialize_success', 0, () => {
        const l3 = createL3();
        const serialized = A4pCodec.serializeIntentL3(l3);

        const result = A4pCodec.deserializeIntentL3(serialized);

        expect(result.intentL2.intentL1.intentId).assertEqual(l3.intentL2.intentL1.intentId);
        expect(result.signingEnv !== undefined).assertTrue();
      });

      it('deserialize_fails_whenEmpty', 0, () => {
        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          A4pCodec.deserializeIntentL3('');
        });
      });

      it('deserialize_fails_whenBodyIsNull', 0, () => {
        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          A4pCodec.deserializeIntentL3('null');
        });
      });

      it('deserialize_fails_whenInvalidJson', 0, () => {
        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          A4pCodec.deserializeIntentL3('{bad json}');
        });
      });

      it('deserialize_fails_whenMissingIntentL2', 0, () => {
        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          A4pCodec.deserializeIntentL3('{"version":"1.0"}');
        });
      });

      it('deserialize_fails_whenMissingSigningEnv', 0, () => {
        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          A4pCodec.deserializeIntentL3('{"intentL2":{},"version":"1.0"}');
        });
      });

      it('deserialize_fails_whenTimestampIsNegative', 0, () => {
        // timestamp must be a non-negative integer.
        const l3 = createL3();
        const serialized = A4pCodec.serializeIntentL3(l3);
        const obj: Record<string, Object> = JSON.parse(serialized);
        obj['timestamp'] = -1;

        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          A4pCodec.deserializeIntentL3(JSON.stringify(obj));
        });
      });

      it('deserialize_fails_whenNestingExceeds100Levels', 0, () => {
        // The parser's side of the 100-level nesting limit (see
        // serialize_fails_whenNestingExceeds100Levels): the Java SDK
        // rejects documents nested deeper than 100 levels, so accepting
        // them here would let the two SDKs disagree about the same
        // bytes. The device chain sits under two containers (root,
        // signingEnv), so a chain of 98 leaves its scalar exactly 100
        // deep and a chain of 99 one past the limit.
        const l3 = createL3();
        const serialized = A4pCodec.serializeIntentL3(l3);
        const withDeviceChain = (levels: number): string =>
          serialized.replace('"device":{"model":"TestDevice","os":"HarmonyOS"}',
            '"device":' + nestedChainJson(levels));

        // At the limit the document parses, and the restored intent
        // re-serializes to the exact input bytes.
        const boundary: string = withDeviceChain(98);
        const restored = A4pCodec.deserializeIntentL3(boundary);
        expect(A4pCodec.serializeIntentL3(restored)).assertEqual(boundary);

        // One level deeper is rejected by the strict parser.
        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          A4pCodec.deserializeIntentL3(withDeviceChain(99));
        });
      });

      it('deserialize_succeeds_whenChainVersionForeign', 0, () => {
        // The whole chain's versions are restored verbatim — the L3's,
        // the L2's and the L1's: even a mixed-version chain is data, and
        // deserialization returns data — the chain-facing APIs (see
        // activateIntent's checkChainVersions) judge versions, not the codec.
        const l3 = createL3();
        const serialized = A4pCodec.serializeIntentL3(l3);

        // The carrier L3's own version.
        const ownVersion: Record<string, Object> = JSON.parse(serialized);
        ownVersion['version'] = '2.0';
        const restoredOwn = A4pCodec.deserializeIntentL3(JSON.stringify(ownVersion));
        expect(restoredOwn.version).assertEqual('2.0');
        expect(restoredOwn.intentL2.intentL1.intentId).assertEqual(l3.intentL2.intentL1.intentId);

        // The embedded L2's version.
        const l2Only: Record<string, Object> = JSON.parse(serialized);
        (l2Only['intentL2'] as Record<string, Object>)['version'] = '2.0';
        const restoredL2 = A4pCodec.deserializeIntentL3(JSON.stringify(l2Only));
        expect(restoredL2.intentL2.version).assertEqual('2.0');

        // The embedded L1's version.
        const l1Only: Record<string, Object> = JSON.parse(serialized);
        const l1OnlyL2: Record<string, Object> = l1Only['intentL2'] as Record<string, Object>;
        (l1OnlyL2['intentL1'] as Record<string, Object>)['version'] = '2.0';
        const restoredL1 = A4pCodec.deserializeIntentL3(JSON.stringify(l1Only));
        expect(restoredL1.intentL2.intentL1.version).assertEqual('2.0');
      });

      it('deserialize_fails_withoutSign', 0, () => {
        const l3 = createL3();
        const serialized = A4pCodec.serializeIntentL3(l3);
        const obj: Record<string, Object> = JSON.parse(serialized);

        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
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

        expectThrows(A4pErrorCode.A4P_SERIALIZE_FAIL, () => {
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
        A4p.createExecution(context, createTestExecution(), Date.now());

        const restored = A4pCodec.deserializeIntentExecutionContext(
          A4pCodec.serializeIntentExecutionContext(context));
        A4p.createExecution(restored, createTestExecution(), Date.now());

        expect(restored.executions.length).assertEqual(2);
        // Two reserved orders of 5000 against the 500000 budget.
        expect(restored.executionInfo.leftAmount).assertEqual(490000);
      });

      it('deserialize_fails_whenEmpty', 0, () => {
        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          A4pCodec.deserializeIntentExecutionContext('');
        });
      });

      it('deserialize_fails_whenBodyIsNull', 0, () => {
        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          A4pCodec.deserializeIntentExecutionContext('null');
        });
      });

      it('deserialize_fails_whenInvalidJson', 0, () => {
        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          A4pCodec.deserializeIntentExecutionContext('{bad json}');
        });
      });

      it('deserialize_fails_whenMissingAuthorizedIntent', 0, () => {
        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          A4pCodec.deserializeIntentExecutionContext(
            '{"intentId":"test","status":"ACTIVE","version":"1.0"}');
        });
      });

      it('deserialize_fails_whenIntentIdMismatched', 0, () => {
        const context = createExecutionContext();
        const serialized = A4pCodec.serializeIntentExecutionContext(context);
        const obj: Record<string, Object> = JSON.parse(serialized);
        obj['intentId'] = 'mismatched-intent-id';

        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          A4pCodec.deserializeIntentExecutionContext(JSON.stringify(obj));
        });
      });

      it('deserialize_fails_whenStatusIsForeign', 0, () => {
        const context = createExecutionContext();
        const serialized = A4pCodec.serializeIntentExecutionContext(context);
        const obj: Record<string, Object> = JSON.parse(serialized);
        obj['status'] = 'PAUSED';

        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          A4pCodec.deserializeIntentExecutionContext(JSON.stringify(obj));
        });
      });

      it('deserialize_fails_whenActivatedTimeMissing', 0, () => {
        const context = createExecutionContext();
        const serialized = A4pCodec.serializeIntentExecutionContext(context);
        const obj: Record<string, Object> = JSON.parse(serialized);

        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          A4pCodec.deserializeIntentExecutionContext(
            JSON.stringify(withoutKey(obj, 'activatedTime')));
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

        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          A4pCodec.deserializeIntentExecutionContext(JSON.stringify(obj));
        });
      });

      it('deserialize_fails_whenRecordStatusIsForeign', 0, () => {
        const context = createExecutionContextWithLedger();
        const serialized = A4pCodec.serializeIntentExecutionContext(context);
        const obj: Record<string, Object> = JSON.parse(serialized);
        const executions: Record<string, Object>[] = obj['executions'] as Record<string, Object>[];
        executions[0]['status'] = 'PAUSED';

        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          A4pCodec.deserializeIntentExecutionContext(JSON.stringify(obj));
        });
      });

      it('deserialize_fails_whenCompletedRecordLacksCompletedAt', 0, () => {
        const context = createExecutionContextWithLedger();
        const serialized = A4pCodec.serializeIntentExecutionContext(context);
        const obj: Record<string, Object> = JSON.parse(serialized);
        const executions: Record<string, Object>[] = obj['executions'] as Record<string, Object>[];
        executions[0] = withoutKey(executions[0], 'completedAt');

        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          A4pCodec.deserializeIntentExecutionContext(JSON.stringify(obj));
        });
      });

      it('deserialize_fails_whenPendingRecordCarriesCompletedAt', 0, () => {
        const context = createExecutionContext();
        A4p.createExecution(context, createTestExecution(), Date.now());
        const serialized = A4pCodec.serializeIntentExecutionContext(context);
        const obj: Record<string, Object> = JSON.parse(serialized);
        const executions: Record<string, Object>[] = obj['executions'] as Record<string, Object>[];
        executions[0]['completedAt'] = 1;
        // Back into canonical order, so the order check stays quiet and
        // the record-shape validation does the rejecting.
        executions[0] = sortedKeysCopy(executions[0]);

        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          A4pCodec.deserializeIntentExecutionContext(JSON.stringify(obj));
        });
      });

      it('deserialize_fails_whenCompletedAtBeforeReservedAt', 0, () => {
        // completeExecution/cancelExecution refuse a clock earlier than the
        // record's reservedAt, so a persisted record violating that
        // monotonic rule is corrupted data: the record's own timeline is
        // judged by the format pass, so restore fails with the uniform 5002.
        const context = createExecutionContextWithLedger();
        const serialized = A4pCodec.serializeIntentExecutionContext(context);
        const obj: Record<string, Object> = JSON.parse(serialized);
        const executions: Record<string, Object>[] = obj['executions'] as Record<string, Object>[];
        const reservedAt: number = executions[0]['reservedAt'] as number;
        executions[0]['completedAt'] = reservedAt - 1;

        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          A4pCodec.deserializeIntentExecutionContext(JSON.stringify(obj));
        });
      });

      it('deserialize_fails_whenPeriodicRecordLacksPeriodIndex', 0, () => {
        // The default test constraints are periodic, so every record must
        // carry its period index.
        const context = createExecutionContext();
        A4p.createExecution(context, createTestExecution(), Date.now());
        const serialized = A4pCodec.serializeIntentExecutionContext(context);
        const obj: Record<string, Object> = JSON.parse(serialized);
        const executions: Record<string, Object>[] = obj['executions'] as Record<string, Object>[];
        executions[0] = withoutKey(executions[0], 'periodIndex');

        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          A4pCodec.deserializeIntentExecutionContext(JSON.stringify(obj));
        });
      });

      it('deserialize_fails_whenNonPeriodicRecordCarriesPeriodIndex', 0, () => {
        const context = createNonPeriodicExecutionContext();
        A4p.createExecution(context, createTestExecution(), Date.now());
        const serialized = A4pCodec.serializeIntentExecutionContext(context);
        const obj: Record<string, Object> = JSON.parse(serialized);
        const executions: Record<string, Object>[] = obj['executions'] as Record<string, Object>[];
        executions[0]['periodIndex'] = 0;
        // Back into canonical order, so the order check stays quiet and
        // the periodicity validation does the rejecting.
        executions[0] = sortedKeysCopy(executions[0]);

        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          A4pCodec.deserializeIntentExecutionContext(JSON.stringify(obj));
        });
      });

      it('deserialize_fails_whenRecordCarriesUnknownField', 0, () => {
        const context = createExecutionContext();
        A4p.createExecution(context, createTestExecution(), Date.now());
        const serialized = A4pCodec.serializeIntentExecutionContext(context);
        const obj: Record<string, Object> = JSON.parse(serialized);
        const executions: Record<string, Object>[] = obj['executions'] as Record<string, Object>[];
        executions[0]['note'] = 'extra';
        // Back into canonical order, so the order check stays quiet and
        // the closed field set does the rejecting.
        executions[0] = sortedKeysCopy(executions[0]);

        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          A4pCodec.deserializeIntentExecutionContext(JSON.stringify(obj));
        });
      });

      it('deserialize_fails_whenExecutionInfoMissing', 0, () => {
        const context = createExecutionContext();
        const serialized = A4pCodec.serializeIntentExecutionContext(context);
        const obj: Record<string, Object> = JSON.parse(serialized);

        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          A4pCodec.deserializeIntentExecutionContext(JSON.stringify(withoutKey(obj, 'executionInfo')));
        });
      });

      it('deserialize_fails_whenPeriodicInfoLacksPeriodFigures', 0, () => {
        const context = createExecutionContext();
        const serialized = A4pCodec.serializeIntentExecutionContext(context);
        const obj: Record<string, Object> = JSON.parse(serialized);
        obj['executionInfo'] = withoutKey(obj['executionInfo'] as Record<string, Object>, 'periodIndex');

        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
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

    // Restore is verbatim: beyond the structural pass above, the
    // deserializer judges no content — versions, ledger reachability,
    // caps, statuses and accounting figures are restored as given. The
    // execution APIs replay the ledger rules on every call, so a
    // restored context's ledger is judged when used, not when parsed;
    // versions are never replayed — only the chain-facing APIs compare
    // them. Each test mutates
    // exactly one wire property and asserts it round-trips untouched.
    describe('IntentExecutionContextRestoreContent', () => {
      it('restore_succeeds_whenAuthorizedChainVersionForeign', 0, () => {
        const serialized = A4pCodec.serializeIntentExecutionContext(createExecutionContext());
        const obj: Record<string, Object> = JSON.parse(serialized);
        wireL3Of(obj)['version'] = '0.9';

        const restored = A4pCodec.deserializeIntentExecutionContext(JSON.stringify(obj));

        expect(restored.authorizedIntent.version).assertEqual('0.9');
      });

      it('restore_succeeds_whenContextVersionForeign', 0, () => {
        // The shape pass only checks the version non-blank; its value is
        // restored as given — a context from a different protocol
        // generation is data the caller decides what to do with.
        const serialized = A4pCodec.serializeIntentExecutionContext(createExecutionContext());
        const obj: Record<string, Object> = JSON.parse(serialized);
        obj['version'] = '9.9';

        const restored = A4pCodec.deserializeIntentExecutionContext(JSON.stringify(obj));

        expect(restored.version).assertEqual('9.9');
      });

      it('restore_succeeds_whenActivatedTimeAfterValidUntil', 0, () => {
        // The activation instant is never replayed against the validity
        // window: a context activated past validUntil restores.
        const serialized = A4pCodec.serializeIntentExecutionContext(createExecutionContext());
        const obj: Record<string, Object> = JSON.parse(serialized);
        const validUntil: number = wireConstraintsOf(obj)['validUntil'] as number;
        obj['activatedTime'] = validUntil + 1;

        const restored = A4pCodec.deserializeIntentExecutionContext(JSON.stringify(obj));

        expect(restored.activatedTime).assertEqual(validUntil + 1);
      });

      it('restore_succeeds_whenReservedAtBeforeActivatedTime', 0, () => {
        // activatedTime is no reservation floor: the reservation restores
        // as given, whatever its relation to the activation instant.
        const serialized = A4pCodec.serializeIntentExecutionContext(createExecutionContextWithPendingLedger());
        const obj: Record<string, Object> = JSON.parse(serialized);
        const beforeActivation: number = (obj['activatedTime'] as number) - 1;
        wireExecutionsOf(obj)[0]['reservedAt'] = beforeActivation;

        const restored = A4pCodec.deserializeIntentExecutionContext(JSON.stringify(obj));

        expect(restored.executions[0].reservedAt).assertEqual(beforeActivation);
      });

      it('restore_succeeds_whenReservedAtBeforeValidFrom', 0, () => {
        // Push validFrom past a record's reservation instant: the window
        // is a lifecycle rule, not a format rule, so the record and the
        // pushed window both restore as given.
        const serialized = A4pCodec.serializeIntentExecutionContext(createExecutionContextWithPendingLedger());
        const obj: Record<string, Object> = JSON.parse(serialized);
        const reservedAt: number = wireExecutionsOf(obj)[0]['reservedAt'] as number;
        wireConstraintsOf(obj)['validFrom'] = reservedAt + 1;

        const restored = A4pCodec.deserializeIntentExecutionContext(JSON.stringify(obj));

        expect(restored.executions[0].reservedAt).assertEqual(reservedAt);
        expect(restored.authorizedIntent.intentL2.intentL1.structuredIntent.constraints.validFrom)
          .assertEqual(reservedAt + 1);
      });

      it('restore_succeeds_whenReservedAtAfterValidUntil', 0, () => {
        const serialized = A4pCodec.serializeIntentExecutionContext(createExecutionContextWithPendingLedger());
        const obj: Record<string, Object> = JSON.parse(serialized);
        const validUntil: number = wireConstraintsOf(obj)['validUntil'] as number;
        wireExecutionsOf(obj)[0]['reservedAt'] = validUntil + 1;

        const restored = A4pCodec.deserializeIntentExecutionContext(JSON.stringify(obj));

        expect(restored.executions[0].reservedAt).assertEqual(validUntil + 1);
      });

      it('restore_succeeds_whenRecordPeriodIndexMismatched', 0, () => {
        // The period index of a record's own reservation instant is a
        // lifecycle derivation, not a format rule: a mismatched index
        // restores as given.
        const serialized = A4pCodec.serializeIntentExecutionContext(createExecutionContextWithLedger());
        const obj: Record<string, Object> = JSON.parse(serialized);
        wireExecutionsOf(obj)[0]['periodIndex'] = 1;

        const restored = A4pCodec.deserializeIntentExecutionContext(JSON.stringify(obj));

        expect(restored.executions[0].periodIndex).assertEqual(1);
      });

      it('restore_succeeds_whenExecutionIdDuplicated', 0, () => {
        // Id uniqueness is a ledger rule createExecution enforces; the
        // codec restores the duplicate verbatim.
        const serialized = A4pCodec.serializeIntentExecutionContext(createExecutionContextWithPendingLedger());
        const obj: Record<string, Object> = JSON.parse(serialized);
        const records: Record<string, Object>[] = wireExecutionsOf(obj);
        records[1]['executionId'] = records[0]['executionId'];

        const restored = A4pCodec.deserializeIntentExecutionContext(JSON.stringify(obj));

        expect(restored.executions[1].executionId).assertEqual(restored.executions[0].executionId);
      });

      it('restore_succeeds_whenOrderIdDuplicated', 0, () => {
        const serialized = A4pCodec.serializeIntentExecutionContext(createExecutionContextWithPendingLedger());
        const obj: Record<string, Object> = JSON.parse(serialized);
        const records: Record<string, Object>[] = wireExecutionsOf(obj);
        wireOrderOf(records[1])['orderId'] = wireOrderOf(records[0])['orderId'];

        const restored = A4pCodec.deserializeIntentExecutionContext(JSON.stringify(obj));

        expect(restored.executions[1].executionContent.orderId)
          .assertEqual(restored.executions[0].executionContent.orderId);
      });

      it('restore_succeeds_whenRecordCurrencyMismatches', 0, () => {
        // Per-record consistency with the signed snapshot (currency
        // included) is a lifecycle rule; the codec restores the mismatch.
        const serialized = A4pCodec.serializeIntentExecutionContext(createExecutionContextWithLedger());
        const obj: Record<string, Object> = JSON.parse(serialized);
        wireOrderOf(wireExecutionsOf(obj)[0])['currency'] = 'USD';

        const restored = A4pCodec.deserializeIntentExecutionContext(JSON.stringify(obj));

        expect(restored.executions[0].executionContent.currency).assertEqual('USD');
      });

      it('restore_succeeds_whenRecordAmountExceedsSingleOrderCap', 0, () => {
        // The single-order cap is a reservation-time rule; an order over
        // it restores as given.
        const serialized = A4pCodec.serializeIntentExecutionContext(createExecutionContextWithLedger());
        const obj: Record<string, Object> = JSON.parse(serialized);
        wireOrderOf(wireExecutionsOf(obj)[0])['amount'] = 100001;

        const restored = A4pCodec.deserializeIntentExecutionContext(JSON.stringify(obj));

        expect(restored.executions[0].executionContent.amount).assertEqual(100001);
      });

      it('restore_succeeds_whenCartQuantityMismatchesTemplate', 0, () => {
        // Template matching is a reservation-time rule; the off-template
        // quantity restores as given.
        const serialized = A4pCodec.serializeIntentExecutionContext(createExecutionContextWithLedger());
        const obj: Record<string, Object> = JSON.parse(serialized);
        const order: Record<string, Object> = wireOrderOf(wireExecutionsOf(obj)[0]);
        const cartItems: Record<string, Object>[] = order['cartItems'] as Record<string, Object>[];
        cartItems[0]['quantity'] = (cartItems[0]['quantity'] as number) + 1;
        const expectedQuantity: number = cartItems[0]['quantity'] as number;

        const restored = A4pCodec.deserializeIntentExecutionContext(JSON.stringify(obj));

        expect(restored.executions[0].executionContent.cartItems[0].quantity).assertEqual(expectedQuantity);
      });

      it('restore_succeeds_whenLedgerExceedsTotalAmount', 0, () => {
        // The ledger holds one COMPLETED order of 5000 (the CANCELED one
        // is excluded); a totalMaxAmount below that is not a cap the
        // ledger could have been produced under — but that judgment
        // belongs to the lifecycle APIs, so the cap restores as given.
        const serialized = A4pCodec.serializeIntentExecutionContext(createExecutionContextWithLedger());
        const obj: Record<string, Object> = JSON.parse(serialized);
        wireConstraintsOf(obj)['totalMaxAmount'] = 4999;

        const restored = A4pCodec.deserializeIntentExecutionContext(JSON.stringify(obj));

        expect(restored.authorizedIntent.intentL2.intentL1.structuredIntent.constraints.totalMaxAmount)
          .assertEqual(4999);
      });

      it('restore_succeeds_whenLedgerExceedsExecutionCount', 0, () => {
        const serialized = A4pCodec.serializeIntentExecutionContext(createExecutionContextWithPendingLedger());
        const obj: Record<string, Object> = JSON.parse(serialized);
        wireConstraintsOf(obj)['maxExecutionCount'] = 1;

        const restored = A4pCodec.deserializeIntentExecutionContext(JSON.stringify(obj));

        expect(restored.authorizedIntent.intentL2.intentL1.structuredIntent.constraints.maxExecutionCount)
          .assertEqual(1);
      });

      it('restore_succeeds_whenLedgerExceedsPeriodExecutionCount', 0, () => {
        const serialized = A4pCodec.serializeIntentExecutionContext(createExecutionContextWithPendingLedger());
        const obj: Record<string, Object> = JSON.parse(serialized);
        wireConstraintsOf(obj)['maxExecutionCountPerPeriod'] = 1;

        const restored = A4pCodec.deserializeIntentExecutionContext(JSON.stringify(obj));

        expect(restored.authorizedIntent.intentL2.intentL1.structuredIntent.constraints.maxExecutionCountPerPeriod)
          .assertEqual(1);
      });

      it('restore_succeeds_whenStatusContradictsLedger', 0, () => {
        // Two PENDING records leave the ledger unexhausted, so a CONSUMED
        // status is not one completeExecution could have produced — but
        // the status agreement is a lifecycle rule, and the codec restores
        // the status as given.
        const serialized = A4pCodec.serializeIntentExecutionContext(createExecutionContextWithPendingLedger());
        const obj: Record<string, Object> = JSON.parse(serialized);
        obj['status'] = 'CONSUMED';

        const restored = A4pCodec.deserializeIntentExecutionContext(JSON.stringify(obj));

        expect(restored.status).assertEqual(IntentExecutionStatus.CONSUMED);
      });

      it('restore_succeeds_whenLeftAmountDisagreesWithLedger', 0, () => {
        // The accounting snapshot is derived state the lifecycle APIs
        // re-derive on every call; a disagreeing stored figure restores
        // as given.
        const serialized = A4pCodec.serializeIntentExecutionContext(createExecutionContextWithPendingLedger());
        const obj: Record<string, Object> = JSON.parse(serialized);
        const info: Record<string, Object> = obj['executionInfo'] as Record<string, Object>;
        info['leftAmount'] = (info['leftAmount'] as number) - 1;
        const expectedLeftAmount: number = info['leftAmount'] as number;

        const restored = A4pCodec.deserializeIntentExecutionContext(JSON.stringify(obj));

        expect(restored.executionInfo.leftAmount).assertEqual(expectedLeftAmount);
      });

      it('restore_succeeds_whenLeftPeriodCountDisagreesWithLedger', 0, () => {
        const serialized = A4pCodec.serializeIntentExecutionContext(createExecutionContextWithPendingLedger());
        const obj: Record<string, Object> = JSON.parse(serialized);
        const info: Record<string, Object> = obj['executionInfo'] as Record<string, Object>;
        info['leftPeriodExecutionCount'] = (info['leftPeriodExecutionCount'] as number) - 1;
        const expectedLeftPeriodCount: number = info['leftPeriodExecutionCount'] as number;

        const restored = A4pCodec.deserializeIntentExecutionContext(JSON.stringify(obj));

        expect(restored.executionInfo.leftPeriodExecutionCount).assertEqual(expectedLeftPeriodCount);
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
        // revokedTime is stamped by revokeIntent:
        // absent on the ACTIVE form, present on the wire of the revoked one.
        expect(jc.indexOf('"revokedTime"')).assertEqual(-1);
        const revoked: IntentExecutionContext = A4p.revokeIntent(ctx, now + 4000);
        const jr: string = A4pCodec.serializeIntentExecutionContext(revoked);
        expect(jr).assertContain('"status":"REVOKED"');
        expect(jr).assertContain('"revokedTime":1700000004000');
      });
    });
  });
}
