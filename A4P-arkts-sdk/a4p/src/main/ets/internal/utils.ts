/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

/**
 * Internal utilities: base64url and UTF-8 codecs, canonical JSON
 * serialization, and deep clone/freeze helpers.
 */

import { A4pErrorCode } from '../models/common';
import { a4pThrow } from './result';

const BASE64URL_CHARS: string = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_';

/**
 * Encodes a byte array to a base64url string (unpadded).
 * @param data - The byte array to encode
 * @returns The base64url-encoded string
 */
export function base64UrlEncode(data: Uint8Array): string {
  let result: string = '';
  const len: number = data.length;
  for (let i = 0; i < len; i += 3) {
    const a: number = data[i];
    const b: number = i + 1 < len ? data[i + 1] : 0;
    const c: number = i + 2 < len ? data[i + 2] : 0;
    result += BASE64URL_CHARS[(a >> 2) & 0x3F];
    result += BASE64URL_CHARS[((a << 4) | (b >> 4)) & 0x3F];
    if (i + 1 < len) {
      result += BASE64URL_CHARS[((b << 2) | (c >> 6)) & 0x3F];
    }
    if (i + 2 < len) {
      result += BASE64URL_CHARS[c & 0x3F];
    }
  }
  return result;
}

/**
 * Decodes a base64url string (unpadded) to a byte array.
 * Rejects non-canonical encodings where unused trailing bits are non-zero.
 * @param str - The base64url string to decode
 * @returns The decoded byte array
 * @throws A4pError with A4P_DESERIALIZE_FAIL on invalid length, character, or non-canonical encoding
 */
export function base64UrlDecode(str: string): Uint8Array {
  const len: number = str.length;
  // A valid base64url string (unpadded) has length % 4 != 1; a trailing
  // group of one character carries no bits and is never produced by encode.
  if (len % 4 === 1) {
    a4pThrow(A4pErrorCode.A4P_DESERIALIZE_FAIL, 'Invalid base64url length: ' + len);
  }
  const byteLen: number = Math.floor(len * 3 / 4);
  const result: Uint8Array = new Uint8Array(byteLen);

  let byteIndex: number = 0;
  for (let i = 0; i < len; i += 4) {
    const a: number = BASE64URL_CHARS.indexOf(str[i]);
    if (a < 0) {
      a4pThrow(A4pErrorCode.A4P_DESERIALIZE_FAIL, 'Invalid base64url character at position ' + i);
    }
    const b: number = i + 1 < len ? BASE64URL_CHARS.indexOf(str[i + 1]) : 0;
    if (i + 1 < len && b < 0) {
      a4pThrow(A4pErrorCode.A4P_DESERIALIZE_FAIL, 'Invalid base64url character at position ' + (i + 1));
    }
    const c: number = i + 2 < len ? BASE64URL_CHARS.indexOf(str[i + 2]) : 0;
    if (i + 2 < len && c < 0) {
      a4pThrow(A4pErrorCode.A4P_DESERIALIZE_FAIL, 'Invalid base64url character at position ' + (i + 2));
    }
    const d: number = i + 3 < len ? BASE64URL_CHARS.indexOf(str[i + 3]) : 0;
    if (i + 3 < len && d < 0) {
      a4pThrow(A4pErrorCode.A4P_DESERIALIZE_FAIL, 'Invalid base64url character at position ' + (i + 3));
    }

    // Reject non-canonical encodings: trailing groups must carry their
    // unused bits as zero, otherwise the same bytes could be encoded in
    // multiple ways and break signature determinism.
    // A 2-char tail encodes 1 byte: the low 4 bits of b are unused.
    if (i + 2 >= len && (b & 0x0F) !== 0) {
      a4pThrow(A4pErrorCode.A4P_DESERIALIZE_FAIL, 'Invalid base64url padding bits at position ' + (i + 1));
    }
    // A 3-char tail encodes 2 bytes: the low 2 bits of c are unused.
    if (i + 3 >= len && (c & 0x03) !== 0) {
      a4pThrow(A4pErrorCode.A4P_DESERIALIZE_FAIL, 'Invalid base64url padding bits at position ' + (i + 2));
    }

    if (byteIndex < byteLen) {
      result[byteIndex++] = ((a << 2) | (b >> 4)) & 0xFF;
    }
    if (byteIndex < byteLen) {
      result[byteIndex++] = ((b << 4) | (c >> 2)) & 0xFF;
    }
    if (byteIndex < byteLen) {
      result[byteIndex++] = ((c << 6) | d) & 0xFF;
    }
  }
  return result;
}

/**
 * Encodes a string to a UTF-8 byte array.
 * @param str - The string to encode
 * @returns The UTF-8 encoded byte array
 * @throws A4pError with A4P_SERIALIZE_FAIL on lone surrogates in the input
 */
export function encodeUtf8(str: string): Uint8Array {
  const bytes: number[] = [];
  for (let i = 0; i < str.length; i++) {
    let c: number = str.charCodeAt(i);
    if (c < 0x80) {
      bytes.push(c);
    } else if (c < 0x800) {
      bytes.push(0xc0 | (c >> 6));
      bytes.push(0x80 | (c & 0x3f));
    } else if (c >= 0xd800 && c <= 0xdbff) {
      const hi: number = c;
      const lo: number = str.charCodeAt(++i);
      // Note: charCodeAt out of bounds returns NaN, and NaN compares false
      // against everything — so the check must be "not in range", not
      // "out of range", to catch both NaN and non-surrogate followers.
      if (!(lo >= 0xdc00 && lo <= 0xdfff)) {
        a4pThrow(A4pErrorCode.A4P_SERIALIZE_FAIL, 'Invalid UTF-16 string: lone surrogate at position ' + (i - 1));
      }
      c = 0x10000 + ((hi - 0xd800) << 10) + (lo - 0xdc00);
      bytes.push(0xf0 | (c >> 18));
      bytes.push(0x80 | ((c >> 12) & 0x3f));
      bytes.push(0x80 | ((c >> 6) & 0x3f));
      bytes.push(0x80 | (c & 0x3f));
    } else {
      bytes.push(0xe0 | (c >> 12));
      bytes.push(0x80 | ((c >> 6) & 0x3f));
      bytes.push(0x80 | (c & 0x3f));
    }
  }
  return new Uint8Array(bytes);
}

/** Reads the UTF-8 continuation byte at index i, returning its 6 data bits. */
function readContinuation(bytes: Uint8Array, i: number): number {
  if (i >= bytes.length) {
    a4pThrow(A4pErrorCode.A4P_DESERIALIZE_FAIL, 'Truncated UTF-8 sequence');
  }
  const b: number = bytes[i];
  if ((b & 0xc0) !== 0x80) {
    a4pThrow(A4pErrorCode.A4P_DESERIALIZE_FAIL, 'Invalid UTF-8 continuation byte at index ' + i);
  }
  return b & 0x3f;
}

/**
 * Decodes a UTF-8 byte array to a string.
 * @param bytes - The UTF-8 byte array to decode
 * @returns The decoded string
 * @throws A4pError with A4P_DESERIALIZE_FAIL on invalid or overlong UTF-8 sequences
 */
export function decodeUtf8(bytes: Uint8Array): string {
  let str: string = '';
  let i: number = 0;
  while (i < bytes.length) {
    const b: number = bytes[i++];
    if (b < 0x80) {
      str += String.fromCharCode(b);
    } else if (b < 0xc0) {
      a4pThrow(A4pErrorCode.A4P_DESERIALIZE_FAIL, 'Unexpected UTF-8 continuation byte at index ' + (i - 1));
    } else if (b < 0xe0) {
      str += String.fromCharCode(((b & 0x1f) << 6) | readContinuation(bytes, i++));
    } else if (b < 0xf0) {
      const c: number = ((b & 0x0f) << 12) | (readContinuation(bytes, i++) << 6) | readContinuation(bytes, i++);
      if (c < 0x800) {
        a4pThrow(A4pErrorCode.A4P_DESERIALIZE_FAIL, 'Overlong UTF-8 encoding at index ' + (i - 3));
      }
      str += String.fromCharCode(c);
    } else {
      const c: number = ((b & 0x07) << 18) | (readContinuation(bytes, i++) << 12) |
        (readContinuation(bytes, i++) << 6) | readContinuation(bytes, i++);
      if (c < 0x10000 || c > 0x10ffff) {
        a4pThrow(A4pErrorCode.A4P_DESERIALIZE_FAIL, 'Overlong or out-of-range UTF-8 encoding at index ' + (i - 4));
      }
      const offset: number = c - 0x10000;
      str += String.fromCharCode(0xd800 + (offset >> 10), 0xdc00 + (offset & 0x3ff));
    }
  }
  return str;
}

/**
 * Serializes a value to canonical JSON: sorted keys, no whitespace,
 * integers only, undefined values omitted.
 * @param value - The value to serialize (plain objects, arrays, primitives)
 * @returns The canonical JSON string
 * @throws A4pError with A4P_SERIALIZE_FAIL on non-plain objects, non-safe integers, or non-finite numbers
 */
export function canonicalStringify(value: Object): string {
  if (value === null || value === undefined) {
    return 'null';
  }
  if (typeof value === 'boolean') {
    return value ? 'true' : 'false';
  }
  if (typeof value === 'number') {
    // Canonical JSON number invariant (see docs/canonical-json-spec.md):
    // every number must be a finite safe integer, rendered as a decimal
    // digit string. Anything else cannot round-trip identically across
    // implementations (e.g. Java Long.toString) and must fail loudly
    // instead of silently normalizing.
    if (!isFinite(value) || Math.floor(value) !== value ||
      value > Number.MAX_SAFE_INTEGER || value < -Number.MAX_SAFE_INTEGER) {
      a4pThrow(A4pErrorCode.A4P_SERIALIZE_FAIL, 'canonicalStringify only supports finite safe integers, got: ' + value);
    }
    return value.toString();
  }
  if (typeof value === 'string') {
    return escapeJsonString(value);
  }
  if (Array.isArray(value)) {
    const items: string[] = [];
    for (let i = 0; i < value.length; i++) {
      items.push(canonicalStringify(value[i] as Object));
    }
    return '[' + items.join(',') + ']';
  }
  if (typeof value === 'object') {
    // Only plain objects have a deterministic key set; anything else
    // (Date, Map, Set, typed arrays, class instances) would silently
    // serialize to "{}" or misordered indices, so reject it outright.
    const proto: object | null = Object.getPrototypeOf(value);
    if (proto !== Object.prototype && proto !== null) {
      a4pThrow(A4pErrorCode.A4P_SERIALIZE_FAIL,
        'canonicalStringify only supports plain objects, arrays and primitives, got: ' +
        (proto ? (proto.constructor ? proto.constructor.name : 'unknown') : 'null prototype'));
    }
    const keys: string[] = Object.keys(value as Record<string, Object>).sort();
    const pairs: string[] = [];
    for (let i = 0; i < keys.length; i++) {
      const k: string = keys[i];
      const v: Object = (value as Record<string, Object>)[k];
      if (v === undefined) {
        continue;
      }
      pairs.push(escapeJsonString(k) + ':' + canonicalStringify(v));
    }
    return '{' + pairs.join(',') + '}';
  }
  return 'null';
}

/** Escapes a string per JSON: quotes, backslash, and control characters. */
function escapeJsonString(s: string): string {
  let result: string = '"';
  for (let i = 0; i < s.length; i++) {
    const c: string = s[i];
    switch (c) {
      case '"':
        result += '\\"';
        break;
      case '\\':
        result += '\\\\';
        break;
      case '\b':
        result += '\\b';
        break;
      case '\f':
        result += '\\f';
        break;
      case '\n':
        result += '\\n';
        break;
      case '\r':
        result += '\\r';
        break;
      case '\t':
        result += '\\t';
        break;
      default:
        if (c.charCodeAt(0) < 0x20) {
          result += '\\u' + c.charCodeAt(0).toString(16).padStart(4, '0');
        } else {
          result += c;
        }
        break;
    }
  }
  return result + '"';
}

/**
 * Strips the top-level `sign` field (the whole SignatureInfo) only:
 * signatures of nested intents (e.g. intentL1.sign inside intentL2)
 * stay in the signed payload so each signature covers the full chain
 * below it.
 */
function stripTopLevelSign(obj: object): Object {
  const src: Record<string, Object> = obj as Record<string, Object>;
  if (!Object.prototype.hasOwnProperty.call(src, 'sign')) {
    return obj as Object;
  }
  const clone: Record<string, Object> = {};
  const keys: string[] = Object.keys(src);
  for (let i = 0; i < keys.length; i++) {
    if (keys[i] !== 'sign') {
      clone[keys[i]] = src[keys[i]];
    }
  }
  return clone as Object;
}

/**
 * Produces the signable bytes for an intent object: canonical JSON with
 * the top-level sign field stripped, encoded as UTF-8.
 * @param obj - The intent object to convert to signable bytes
 * @returns The UTF-8 encoded canonical JSON bytes (without top-level sign)
 */
export function toSignableBytes(obj: object): Uint8Array {
  const canonical: string = canonicalStringify(stripTopLevelSign(obj));
  return encodeUtf8(canonical);
}

/**
 * Deep-copies a plain-object tree (arrays and object literals only).
 * Used when embedding a lower-level intent into a higher-level one so the
 * two do not share references: mutating the caller's original after
 * issuance would silently invalidate the covering signature.
 * @param value - The value to deep-copy
 * @returns A deep copy of the value
 */
export function deepClone<T>(value: T): T {
  if (value === null || typeof value !== 'object') {
    return value;
  }
  if (Array.isArray(value)) {
    const arr: Object[] = [];
    for (let i = 0; i < value.length; i++) {
      arr.push(deepClone(value[i] as Object));
    }
    return arr as Object as T;
  }
  const src: Record<string, Object> = value as Record<string, Object>;
  const out: Record<string, Object> = {};
  const keys: string[] = Object.keys(src);
  for (let i = 0; i < keys.length; i++) {
    out[keys[i]] = deepClone(src[keys[i]]);
  }
  return out as Object as T;
}

/**
 * Recursively freezes a plain-object tree. This is protection against
 * accidental mutation of the authorization snapshot, not a cryptographic
 * boundary — a determined caller can still work around Object.freeze.
 * @param value - The value to deep-freeze
 * @returns The frozen value (same reference, now immutable)
 */
export function deepFreeze<T>(value: T): T {
  if (value === null || typeof value !== 'object') {
    return value;
  }
  if (Array.isArray(value)) {
    for (let i = 0; i < value.length; i++) {
      deepFreeze(value[i] as Object);
    }
    Object.freeze(value);
    return value;
  }
  const src: Record<string, Object> = value as Record<string, Object>;
  const keys: string[] = Object.keys(src);
  for (let i = 0; i < keys.length; i++) {
    deepFreeze(src[keys[i]]);
  }
  Object.freeze(value);
  return value;
}
