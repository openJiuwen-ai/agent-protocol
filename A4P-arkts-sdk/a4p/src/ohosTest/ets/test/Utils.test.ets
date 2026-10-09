/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

import { describe, it, expect } from '@ohos/hypium';
import { A4pError, A4pErrorCode } from '../../../../Index';
import {
  base64UrlEncode, base64UrlDecode,
  encodeUtf8, decodeUtf8,
  canonicalStringify
} from '../../../../src/main/ets/internal/utils';

// Class instance used to verify canonicalStringify rejects non-plain objects.
class CustomInfo {
  /** Arbitrary payload field */
  field: string = 'value';
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

function arraysEqual(a: Uint8Array, b: Uint8Array): boolean {
  if (a.length !== b.length) {
    return false;
  }
  for (let i = 0; i < a.length; i++) {
    if (a[i] !== b[i]) {
      return false;
    }
  }
  return true;
}

export default function utilsTest() {
  describe('UtilsTest', () => {

    describe('base64Url', () => {
      it('roundTrip_lengthMod3Is0', 0, () => {
        const data: Uint8Array = new Uint8Array([0x61, 0x62, 0x63]); // 'abc'

        const encoded: string = base64UrlEncode(data);
        const decoded: Uint8Array = base64UrlDecode(encoded);

        expect(encoded).assertEqual('YWJj');
        expect(arraysEqual(decoded, data)).assertTrue();
      });

      it('roundTrip_lengthMod3Is1', 0, () => {
        const data: Uint8Array = new Uint8Array([0x61]); // 'a'

        const encoded: string = base64UrlEncode(data);
        const decoded: Uint8Array = base64UrlDecode(encoded);

        expect(encoded).assertEqual('YQ');
        expect(arraysEqual(decoded, data)).assertTrue();
      });

      it('roundTrip_lengthMod3Is2', 0, () => {
        const data: Uint8Array = new Uint8Array([0x61, 0x62]); // 'ab'

        const encoded: string = base64UrlEncode(data);
        const decoded: Uint8Array = base64UrlDecode(encoded);

        expect(encoded).assertEqual('YWI');
        expect(arraysEqual(decoded, data)).assertTrue();
      });

      it('roundTrip_empty', 0, () => {
        const data: Uint8Array = new Uint8Array(0);

        const encoded: string = base64UrlEncode(data);
        const decoded: Uint8Array = base64UrlDecode(encoded);

        expect(encoded).assertEqual('');
        expect(decoded.length).assertEqual(0);
      });

      it('roundTrip_allByteValues', 0, () => {
        const data: Uint8Array = new Uint8Array(256);
        for (let i = 0; i < 256; i++) {
          data[i] = i;
        }

        const decoded: Uint8Array = base64UrlDecode(base64UrlEncode(data));

        expect(arraysEqual(decoded, data)).assertTrue();
      });

      it('encode_usesUrlSafeAlphabet', 0, () => {
        // Bytes 0xFB..0xFF encode using the '+' and '/' positions in
        // standard base64; base64url must map them to '-' and '_'.
        const data: Uint8Array = new Uint8Array([0xFB, 0xFF, 0xBF]);

        const encoded: string = base64UrlEncode(data);

        expect(encoded.indexOf('+') < 0).assertTrue();
        expect(encoded.indexOf('/') < 0).assertTrue();
      });

      it('decode_fails_whenInvalidCharacter', 0, () => {
        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          base64UrlDecode('a*b!');
        });
      });

      it('decode_fails_whenLengthMod4Is1', 0, () => {
        // A single trailing character carries no bits; valid base64url
        // strings never have length % 4 == 1 (e.g. 'Y', 'abcdY').
        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          base64UrlDecode('Y');
        });
        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          base64UrlDecode('YWJjY');
        });
      });

      it('decode_fails_whenPaddingBitsNonZero', 0, () => {
        // 'YQ' encodes byte 0x61 ('a'); 'YR' has the same length but
        // non-zero trailing bits — a non-canonical encoding that must be
        // rejected so base64url strings remain unique per byte sequence.
        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          base64UrlDecode('YR');
        });

        // Two-character tail with non-zero padding bits: 'YWJ' encodes
        // 'ab' (0x61 0x62); 'YWJ' + trailing-bit variants like 'YWK' must
        // also be rejected.
        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          base64UrlDecode('YWK');
        });
      });
    });

    describe('utf8', () => {
      it('roundTrip_ascii', 0, () => {
        const str: string = 'hello a4p';

        expect(decodeUtf8(encodeUtf8(str))).assertEqual(str);
      });

      it('roundTrip_twoByteChars', 0, () => {
        const str: string = '中文测试';

        expect(decodeUtf8(encodeUtf8(str))).assertEqual(str);
      });

      it('roundTrip_threeByteChars', 0, () => {
        const str: string = '€ℓ∫';

        expect(decodeUtf8(encodeUtf8(str))).assertEqual(str);
      });

      it('roundTrip_fourByteSurrogatePair', 0, () => {
        const str: string = 'emoji: 😀'; // U+1F600

        const encoded: Uint8Array = encodeUtf8(str);
        expect(encoded.length).assertEqual(11); // 'emoji: ' is 7 bytes + 4-byte char

        expect(decodeUtf8(encoded)).assertEqual(str);
      });

      it('roundTrip_empty', 0, () => {
        expect(encodeUtf8('').length).assertEqual(0);
        expect(decodeUtf8(new Uint8Array(0))).assertEqual('');
      });

      it('encode_fails_whenLoneSurrogate', 0, () => {
        // A high surrogate with no following low surrogate is invalid UTF-16;
        // encoding must throw instead of producing NaN bytes.
        expectThrows(A4pErrorCode.A4P_SERIALIZE_FAIL, () => {
          encodeUtf8('\uD800');
        });
      });

      it('decode_fails_whenContinuationByteMissing', 0, () => {
        // 0xC3 expects one continuation byte; none follows.
        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          decodeUtf8(new Uint8Array([0xC3]));
        });
      });

      it('decode_fails_whenContinuationByteIsAscii', 0, () => {
        // 0xC3 must be followed by 0x80..0xBF; 'a' is not one.
        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          decodeUtf8(new Uint8Array([0xC3, 0x61]));
        });
      });

      it('decode_fails_whenLeadingContinuationByte', 0, () => {
        // A bare continuation byte cannot start a sequence.
        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          decodeUtf8(new Uint8Array([0x80]));
        });
      });

      it('decode_fails_whenOverlongEncoding', 0, () => {
        // 0xE0 0x80 0x80 decodes to U+0000 — overlong form of a 1-byte char.
        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          decodeUtf8(new Uint8Array([0xE0, 0x80, 0x80]));
        });
      });
    });

    describe('canonicalStringify', () => {
      it('serializesPrimitives', 0, () => {
        expect(canonicalStringify(true as Object)).assertEqual('true');
        expect(canonicalStringify(false as Object)).assertEqual('false');
        expect(canonicalStringify(42 as Object)).assertEqual('42');
        expect(canonicalStringify('str' as Object)).assertEqual('"str"');
      });

      it('nonFiniteNumbersThrow', 0, () => {
        // Non-finite numbers must fail loudly (spec §1): silently
        // normalizing them would let invalid payloads get signed.
        expectThrows(A4pErrorCode.A4P_SERIALIZE_FAIL, () => {
          canonicalStringify(NaN as Object);
        });
        expectThrows(A4pErrorCode.A4P_SERIALIZE_FAIL, () => {
          canonicalStringify(Infinity as Object);
        });
        expectThrows(A4pErrorCode.A4P_SERIALIZE_FAIL, () => {
          canonicalStringify(-Infinity as Object);
        });
      });

      it('nonIntegerNumbersThrow', 0, () => {
        expectThrows(A4pErrorCode.A4P_SERIALIZE_FAIL, () => {
          canonicalStringify(1.5 as Object);
        });
      });

      it('unsafeIntegersThrow', 0, () => {
        // 2^53 exceeds the safe-integer range; JS would print it in full
        // but the value already lost precision when parsed, and other
        // languages (Java long) disagree — must be rejected.
        expectThrows(A4pErrorCode.A4P_SERIALIZE_FAIL, () => {
          canonicalStringify(9007199254740992 as Object);
        });
        expectThrows(A4pErrorCode.A4P_SERIALIZE_FAIL, () => {
          canonicalStringify(1e21 as Object);
        });
      });

      it('maxSafeIntegerRoundTrips', 0, () => {
        expect(canonicalStringify(9007199254740991 as Object)).assertEqual('9007199254740991');
        expect(canonicalStringify(-9007199254740991 as Object)).assertEqual('-9007199254740991');
        expect(canonicalStringify(0 as Object)).assertEqual('0');
      });

      it('sortsKeysByCodePoint', 0, () => {
        const obj: Record<string, Object> = { 'b': 1, 'a': 2 };

        expect(canonicalStringify(obj as Object)).assertEqual('{"a":2,"b":1}');
      });

      it('omitsUndefinedProperties', 0, () => {
        const obj: Record<string, Object> = { 'b': 1 };
        // Assign undefined the way optional fields end up absent.
        const obj2: Record<string, Object | undefined> = { 'a': undefined, 'b': 1 };

        expect(canonicalStringify(obj as Object)).assertEqual('{"b":1}');
        expect(canonicalStringify(obj2 as Object)).assertEqual('{"b":1}');
      });

      it('keepsArrayOrder', 0, () => {
        const arr: Object[] = [3, 1, 2];

        expect(canonicalStringify(arr as Object)).assertEqual('[3,1,2]');
      });

      it('escapesSpecialCharacters', 0, () => {
        const result: string = canonicalStringify('a"b\\c\nd\te' as Object);

        expect(result).assertEqual('"a\\"b\\\\c\\nd\\te"');
      });

      it('escapesControlCharactersAsUnicode', 0, () => {
        const result: string = canonicalStringify('x\x01y' as Object);

        expect(result).assertEqual('"x\\u0001y"');
      });

      it('escapesOtherSpecialChars', 0, () => {
        const result: string = canonicalStringify('\b\f\r' as Object);

        expect(result).assertEqual('"\\b\\f\\r"');
      });

      it('fails_whenValueIsClassInstance', 0, () => {
        expectThrows(A4pErrorCode.A4P_SERIALIZE_FAIL, () => {
          canonicalStringify(new CustomInfo() as Object);
        });
      });

      it('specVectors_nonAsciiKeysAndValues', 0, () => {
        // From docs/canonical-json-spec.md §5 (shared with the Java impl).
        const obj: Record<string, Object> = { '中': '文' };
        expect(canonicalStringify(obj as Object)).assertEqual('{"中":"文"}');
      });
    });
  });
}
