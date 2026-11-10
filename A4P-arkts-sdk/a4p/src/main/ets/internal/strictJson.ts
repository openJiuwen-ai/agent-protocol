/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

import { A4pErrorCode } from '../models/common';
import { a4pThrow } from './result';

/**
 * Nesting limit shared by this parser and the canonical serializer (utils.ts
 * imports it): a value may sit at most this many containers deep. The Java
 * SDK enforces the same 100-level limit on both directions (its CanonicalJson
 * normalize rejects deeper documents), so a deeper document fails
 * deserialization there — accepting it here would let the two SDKs disagree
 * about the same bytes.
 */
export const MAX_JSON_DEPTH: number = 100;

/** Protocol numbers must round-trip exactly; anything else is malformed. */
const MAX_SAFE_INTEGER: number = 9007199254740991;

/**
 * Strict JSON parser for protocol deserialization. Unlike JSON.parse it
 * rejects, with A4P_DESERIALIZE_FAIL:
 * - duplicate object keys (JSON.parse silently keeps the last one, so a
 *   payload like {"a":1,"a":2} would otherwise deserialize differently
 *   than it reads, and never identically to the bytes a signer signed);
 * - object keys out of canonical order: the canonical serializer emits
 *   every object's keys sorted by UTF-16 code units, and the input must
 *   be those exact bytes, so each key must be strictly greater than the
 *   one before it — a reordered but otherwise well-formed document is
 *   rejected rather than silently re-canonicalized (key order carries
 *   no protocol meaning, but bytes no signer signed must not pass as
 *   though they had been);
 * - explicit null literals inside objects and arrays: canonical protocol
 *   JSON carries no nulls — an absent optional field is omitted, not
 *   spelled null — so a present null is neither a value nor an absence
 *   the protocol defines;
 * - numbers that are not plain decimal safe integers: fraction and
 *   exponent spellings are rejected even when they evaluate to an
 *   integer, as are overflow (1e999), silent rounding
 *   (9007199254740993) and negative zero — the parsed value must be
 *   exactly what the bytes spelled;
 * - the "__proto__" object key: assigning it through bracket notation
 *   replaces the object's prototype instead of creating an own property,
 *   which would hide the entry from enumeration while it still leaks
 *   into property reads;
 * - unpaired surrogate halves, raw: a lone half builds a string no strict
 *   UTF-8 encoder accepts, so it fails at parse time rather than at signing
 *   (escaped halves, paired or not, fail as non-canonical spellings below);
 * - non-canonical escape spellings: the canonical serializer emits exactly
 *   one spelling per character — `\"`, `\\`, `\b`, `\f`, `\n`, `\r`, `\t`,
 *   lowercase `\u00xx` for the remaining control characters below 0x20,
 *   and every other character raw — so `\/` (the solidus is spelled raw),
 *   a `\uXXXX` escape of any character at or above 0x20 (astral characters
 *   included: an escaped surrogate pair is rejected, not combined), a
 *   `\uXXXX` spelling of one of the five short-escape characters, and
 *   uppercase hex digits are all rejected rather than silently
 *   re-canonicalized, matching the Java SDK, which accepts a document only
 *   when it equals its canonical re-serialization byte for byte;
 * - whitespace, anywhere: the canonical form carries none — no space,
 *   tab, newline or carriage return before, between or after tokens —
 *   so the input must be the exact bytes the canonical serializer
 *   emits; padded or pretty-printed documents are rejected rather
 *   than silently re-canonicalized;
 * - trailing content after the top-level value, malformed numbers
 *   (leading zeros, bare '.', truncated exponents), raw control
 *   characters in strings, invalid escapes, and nesting deeper than
 *   MAX_JSON_DEPTH (100 levels, the Java SDK's shared limit).
 *
 * The one admitted null is the bare document "null": it parses to a null
 * root so the structural validators reject it as invalid data — which
 * the codec surfaces as A4P_DESERIALIZE_FAIL — never as a raw TypeError
 * from dereferencing it.
 */
export function parseStrictJson(text: string): Object | null {
  return new StrictJsonParser(text).parseDocument();
}

/**
 * Recursive-descent parser implementing the strict protocol JSON grammar.
 * Positions are character (UTF-16 code unit) indexes into the input text.
 */
class StrictJsonParser {
  private readonly text: string;
  private pos: number;

  /**
   * @param text - The JSON text to parse
   */
  constructor(text: string) {
    this.text = text;
    this.pos = 0;
  }

  /**
   * Parses the whole document: one JSON value filling the text exactly —
   * no byte is tolerated before or after it.
   * @returns The parsed value; the bare document "null" yields a null root
   */
  parseDocument(): Object | null {
    if (this.peekChar() === 'n') {
      this.expectWord('null');
      if (this.pos < this.text.length) {
        this.fail('trailing content after root null, at position ' + this.pos);
      }
      return null;
    }
    const value: Object = this.parseValue(0);
    if (this.pos < this.text.length) {
      this.fail('trailing content at position ' + this.pos);
    }
    return value;
  }

  /** Parses one JSON value at the current position (null literals rejected). */
  private parseValue(depth: number): Object {
    if (depth > MAX_JSON_DEPTH) {
      this.fail('nesting deeper than ' + MAX_JSON_DEPTH + ' levels');
    }
    const c: string = this.peekChar();
    if (c === '{') {
      return this.parseObject(depth);
    }
    if (c === '[') {
      return this.parseArray(depth);
    }
    if (c === '"') {
      return this.parseString();
    }
    if (c === 't') {
      this.expectWord('true');
      return true as Object;
    }
    if (c === 'f') {
      this.expectWord('false');
      return false as Object;
    }
    if (c === '-' || this.isDigit(c)) {
      return this.parseNumber() as Object;
    }
    if (c === 'n') {
      this.expectWord('null');
      this.fail('explicit null is not part of the canonical protocol JSON — omit the field instead (position ' +
        this.pos + ')');
    }
    if (c === ' ' || c === '\t' || c === '\n' || c === '\r') {
      this.fail('whitespace at position ' + this.pos +
        ' is not part of the canonical protocol JSON — the input must be the exact canonical serialization');
    }
    this.fail('unexpected character \'' + c + '\' at position ' + this.pos);
  }

  /**
   * Parses an object: string keys, unique (duplicates rejected), each
   * followed by ':' and a value. Keys must arrive in canonical order —
   * strictly increasing by UTF-16 code units, the comparison
   * canonicalStringify's key sort uses — so a document that reorders
   * them is rejected instead of being silently re-canonicalized.
   */
  private parseObject(depth: number): Record<string, Object> {
    this.pos++; // opening '{'
    const result: Record<string, Object> = {};
    if (this.peekChar() === '}') {
      this.pos++;
      return result;
    }
    let prevKey: string | null = null;
    while (true) {
      if (this.peekChar() !== '"') {
        this.fail('expected an object key at position ' + this.pos);
      }
      const keyStart: number = this.pos;
      const key: string = this.parseString();
      if (key === '__proto__') {
        this.fail('\'__proto__\' is not a valid protocol field name');
      }
      if (Object.prototype.hasOwnProperty.call(result, key)) {
        this.fail('duplicate object key \'' + key + '\'');
      }
      // Canonical order: strictly increasing by UTF-16 code units, the
      // same comparison Array.prototype.sort (and canonicalStringify's
      // key sort) applies. Together with the duplicate rejection above
      // this pins the key sequence to exactly the sorted sequence; the
      // duplicate check runs first so a repeated key still reports as a
      // duplicate, not as an ordering violation.
      if (prevKey !== null && key <= prevKey) {
        this.fail('object key \'' + key + '\' at position ' + keyStart +
          ' is out of canonical order — keys must appear in strictly increasing order' +
          ' (previous key: \'' + prevKey + '\')');
      }
      prevKey = key;
      if (this.peekChar() !== ':') {
        this.fail('expected \':\' at position ' + this.pos);
      }
      this.pos++;
      result[key] = this.parseValue(depth + 1);
      const c: string = this.peekChar();
      if (c === ',') {
        this.pos++;
        continue;
      }
      if (c === '}') {
        this.pos++;
        return result;
      }
      this.fail('expected \',\' or \'}\' at position ' + this.pos);
    }
  }

  /** Parses an array: values separated by ',', the last followed by ']'. */
  private parseArray(depth: number): Object[] {
    this.pos++; // opening '['
    const result: Object[] = [];
    if (this.peekChar() === ']') {
      this.pos++;
      return result;
    }
    while (true) {
      result.push(this.parseValue(depth + 1));
      const c: string = this.peekChar();
      if (c === ',') {
        this.pos++;
        continue;
      }
      if (c === ']') {
        this.pos++;
        return result;
      }
      this.fail('expected \',\' or \']\' at position ' + this.pos);
    }
  }

  /**
   * Parses a string literal: the caller has verified the opening '"'.
   * Escapes must match the canonical serializer's exact spellings (see
   * parseEscape); raw control characters and unpaired surrogate halves
   * are rejected.
   */
  private parseString(): string {
    this.pos++; // opening '"'
    let result: string = '';
    while (true) {
      if (this.pos >= this.text.length) {
        this.fail('unterminated string');
      }
      const c: string = this.text[this.pos];
      if (c === '"') {
        this.pos++;
        return result;
      }
      if (c === '\\') {
        result += this.parseEscape();
        continue;
      }
      const code: number = c.charCodeAt(0);
      if (code < 0x20) {
        this.fail('raw control character in string at position ' + this.pos);
      }
      if (code >= 0xd800 && code <= 0xdfff) {
        // A raw surrogate half must be followed by the raw complementary
        // half: a lone half builds a string no strict UTF-8 encoder accepts.
        const next: string = this.pos + 1 < this.text.length ? this.text[this.pos + 1] : '';
        const nextCode: number = next.charCodeAt(0); // NaN on '', compares false
        if (code > 0xdbff || !(nextCode >= 0xdc00 && nextCode <= 0xdfff)) {
          this.fail('lone surrogate at position ' + this.pos);
        }
        result += c + next;
        this.pos += 2;
        continue;
      }
      result += c;
      this.pos++;
    }
  }

  /**
   * Parses one escape sequence; the caller has consumed the '\'. Only the
   * canonical spellings are admitted: the serializer emits exactly one
   * spelling per character — `\"`, `\\`, `\b`, `\f`, `\n`, `\r`, `\t` and
   * lowercase `\u00xx` for the remaining control characters below 0x20 —
   * and the input must be those exact bytes. Any other escape is a
   * spelling the canonical form never carries — `\/` (the solidus is
   * spelled raw), a `\uXXXX` of a character at or above 0x20 (astral
   * characters included: an escaped surrogate pair spells a character
   * the canonical form writes raw, and a lone escaped half is rejected
   * before the surrogate rules even apply), a `\uXXXX` spelling of one
   * of the five short-escape characters, or uppercase hex digits — and
   * is rejected rather than silently re-canonicalized, matching the
   * Java SDK, which accepts a document only when it equals its own
   * canonical re-serialization.
   */
  private parseEscape(): string {
    if (this.pos + 1 >= this.text.length) {
      this.fail('unterminated escape at position ' + this.pos);
    }
    const escapeStart: number = this.pos;
    const e: string = this.text[this.pos + 1];
    this.pos += 2;
    if (e === '"') {
      return '"';
    }
    if (e === '\\') {
      return '\\';
    }
    if (e === 'b') {
      return '\b';
    }
    if (e === 'f') {
      return '\f';
    }
    if (e === 'n') {
      return '\n';
    }
    if (e === 'r') {
      return '\r';
    }
    if (e === 't') {
      return '\t';
    }
    if (e === '/') {
      // Valid JSON, but not a spelling the canonical form carries: the
      // serializer writes the solidus raw, so the input must too.
      this.fail('non-canonical escape \\/ at position ' + escapeStart +
        ' — the canonical form spells the solidus raw');
    }
    if (e === 'u') {
      const code: number = this.readHex4(escapeStart);
      // Canonical \u escapes exist only for the control characters with no
      // short spelling: the serializer emits \b, \t, \n, \f and \r for
      // those five and every character from 0x20 up raw, so any other code
      // point — an ordinary character, a short-escape one, or a surrogate
      // half — has a different canonical spelling and its \u form is
      // rejected here.
      if (code >= 0x20 || code === 0x08 || code === 0x09 || code === 0x0a ||
        code === 0x0c || code === 0x0d) {
        this.fail('non-canonical unicode escape at position ' + escapeStart +
          ' — characters at or above 0x20 and the five short-escape characters ' +
          'carry different canonical spellings (raw and \\b\\t\\n\\f\\r)');
      }
      return String.fromCharCode(code);
    }
    this.fail('invalid escape character \'' + e + '\' at position ' + escapeStart);
  }

  /**
   * Reads the four hex digits of a \u escape; escapeStart is the error
   * position. Uppercase hex digits are rejected as a non-canonical
   * spelling: the serializer renders control characters with lowercase
   * digits only (toString(16) emits lowercase), and the input must be
   * those exact bytes.
   */
  private readHex4(escapeStart: number): number {
    if (this.pos + 4 > this.text.length) {
      this.fail('truncated unicode escape at position ' + escapeStart);
    }
    const hex: string = this.text.substring(this.pos, this.pos + 4);
    let code: number = 0;
    for (let i = 0; i < 4; i++) {
      const d: string = hex[i];
      let v: number = -1;
      if (d >= '0' && d <= '9') {
        v = d.charCodeAt(0) - 48;
      } else if (d >= 'a' && d <= 'f') {
        v = d.charCodeAt(0) - 87;
      } else if (d >= 'A' && d <= 'F') {
        this.fail('non-canonical unicode escape at position ' + escapeStart +
          ' — canonical \\u escapes spell their hex digits in lowercase');
      }
      if (v < 0) {
        this.fail('invalid unicode escape at position ' + escapeStart);
      }
      code = code * 16 + v;
    }
    this.pos += 4;
    return code;
  }

  /**
   * Parses a number per the RFC 8259 grammar, then accepts only plain
   * decimal safe integers: fraction and exponent spellings are rejected
   * even when they evaluate to an integer, as are overflow (1e999) and
   * unsafe integers that would silently round (9007199254740993).
   */
  private parseNumber(): number {
    const start: number = this.pos;
    if (this.peekChar() === '-') {
      this.pos++;
    }
    if (this.peekChar() === '0') {
      this.pos++;
    } else if (this.isDigit(this.peekChar())) {
      while (this.isDigit(this.peekChar())) {
        this.pos++;
      }
    } else {
      this.fail('invalid number at position ' + start);
    }
    if (this.peekChar() === '.') {
      this.pos++;
      if (!this.isDigit(this.peekChar())) {
        this.fail('invalid number at position ' + start);
      }
      while (this.isDigit(this.peekChar())) {
        this.pos++;
      }
    }
    const exp: string = this.peekChar();
    if (exp === 'e' || exp === 'E') {
      this.pos++;
      const sign: string = this.peekChar();
      if (sign === '+' || sign === '-') {
        this.pos++;
      }
      if (!this.isDigit(this.peekChar())) {
        this.fail('invalid number at position ' + start);
      }
      while (this.isDigit(this.peekChar())) {
        this.pos++;
      }
    }
    const literal: string = this.text.substring(start, this.pos);
    // The spelling must be a plain integer: Number() silently rounds
    // some fraction and exponent literals (1e-325 to 0), which would
    // make the parsed value differ from the bytes and let distinct
    // spellings collide on one canonical form.
    if (literal.indexOf('.') >= 0 || literal.indexOf('e') >= 0 || literal.indexOf('E') >= 0) {
      this.fail('number is not a plain decimal integer at position ' + start +
        ' (protocol numbers are plain decimal safe integers; non-integer values are carried as strings)');
    }
    const value: number = Number(literal);
    // Range check: rejects ±Infinity and unsafe integers that would
    // round on assignment (9007199254740993).
    if (!(value >= -MAX_SAFE_INTEGER && value <= MAX_SAFE_INTEGER)) {
      this.fail('number is not a safe integer at position ' + start +
        ' (protocol numbers are safe integers; non-integer values are carried as strings)');
    }
    // Negative zero is rejected: the canonical form spells zero only "0"
    // (the serializer renders -0 as 0).
    if (value === 0 && literal[0] === '-') {
      this.fail('negative zero is not part of the canonical protocol JSON at position ' + start);
    }
    return value;
  }

  /** Consumes the expected literal word ("true"/"false"/"null") or fails. */
  private expectWord(word: string): void {
    if (this.text.substring(this.pos, this.pos + word.length) === word) {
      this.pos += word.length;
      return;
    }
    this.fail('invalid literal at position ' + this.pos);
  }

  /** The character at the current position, or '' past the end. */
  private peekChar(): string {
    if (this.pos >= this.text.length) {
      return '';
    }
    return this.text[this.pos];
  }

  /** Whether c is an ASCII digit ('' and non-digits are not). */
  private isDigit(c: string): boolean {
    return c >= '0' && c <= '9';
  }

  /** Fails the parse with the given cause (logged, not thrown). */
  private fail(detail: string): never {
    a4pThrow(A4pErrorCode.A4P_DESERIALIZE_FAIL, detail);
  }
}
