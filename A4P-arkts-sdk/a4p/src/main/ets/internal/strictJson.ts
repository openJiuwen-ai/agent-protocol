/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

import { A4pErrorCode } from '../models/common';
import { a4pThrow } from './result';

/** Nesting limit for parsed documents, a guard against stack exhaustion. */
const MAX_JSON_DEPTH: number = 1000;

/** Protocol numbers must round-trip exactly; anything else is malformed. */
const MAX_SAFE_INTEGER: number = 9007199254740991;

/**
 * Strict JSON parser for protocol deserialization. Unlike JSON.parse it
 * rejects, with A4P_DESERIALIZE_FAIL:
 * - duplicate object keys (JSON.parse silently keeps the last one, so a
 *   payload like {"a":1,"a":2} would otherwise deserialize differently
 *   than it reads, and never identically to the bytes a signer signed);
 * - explicit null literals inside objects and arrays: canonical protocol
 *   JSON carries no nulls — an absent optional field is omitted, not
 *   spelled null — so a present null is neither a value nor an absence
 *   the protocol defines;
 * - numbers that are not plain decimal safe integers: fraction and
 *   exponent spellings are rejected even when they evaluate to an
 *   integer, as are overflow (1e999) and silent rounding
 *   (9007199254740993) — the parsed value must be exactly what the
 *   bytes spelled;
 * - the "__proto__" object key: assigning it through bracket notation
 *   replaces the object's prototype instead of creating an own property,
 *   which would hide the entry from enumeration while it still leaks
 *   into property reads;
 * - unpaired surrogate halves, raw or escaped: a lone half builds a
 *   string no strict UTF-8 encoder accepts, so it fails at parse time
 *   rather than at signing;
 * - trailing content after the top-level value, malformed numbers
 *   (leading zeros, bare '.', truncated exponents), raw control
 *   characters in strings, invalid escapes, and nesting deeper than
 *   MAX_JSON_DEPTH.
 *
 * The one normalized spelling is "-0", which reads as 0: the canonical
 * form spells both zeros "0", and Java's long has no negative zero.
 *
 * The one admitted null is the bare document "null": it parses to a null
 * root so the structural validators report it as A4P_INVALID_PARAMETER,
 * never as a raw TypeError from dereferencing it.
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
   * Parses the whole document: one JSON value followed only by whitespace.
   * @returns The parsed value; the bare document "null" yields a null root
   */
  parseDocument(): Object | null {
    this.skipWhitespace();
    if (this.peekChar() === 'n') {
      this.expectWord('null');
      this.skipWhitespace();
      if (this.pos < this.text.length) {
        this.fail('trailing content after root null, at position ' + this.pos);
      }
      return null;
    }
    const value: Object = this.parseValue(0);
    this.skipWhitespace();
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
    this.fail('unexpected character \'' + c + '\' at position ' + this.pos);
  }

  /**
   * Parses an object: string keys, unique (duplicates rejected), each
   * followed by ':' and a value.
   */
  private parseObject(depth: number): Record<string, Object> {
    this.pos++; // opening '{'
    const result: Record<string, Object> = {};
    this.skipWhitespace();
    if (this.peekChar() === '}') {
      this.pos++;
      return result;
    }
    while (true) {
      this.skipWhitespace();
      if (this.peekChar() !== '"') {
        this.fail('expected an object key at position ' + this.pos);
      }
      const key: string = this.parseString();
      if (key === '__proto__') {
        this.fail('\'__proto__\' is not a valid protocol field name');
      }
      if (Object.prototype.hasOwnProperty.call(result, key)) {
        this.fail('duplicate object key \'' + key + '\'');
      }
      this.skipWhitespace();
      if (this.peekChar() !== ':') {
        this.fail('expected \':\' at position ' + this.pos);
      }
      this.pos++;
      this.skipWhitespace();
      result[key] = this.parseValue(depth + 1);
      this.skipWhitespace();
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
    this.skipWhitespace();
    if (this.peekChar() === ']') {
      this.pos++;
      return result;
    }
    while (true) {
      this.skipWhitespace();
      result.push(this.parseValue(depth + 1));
      this.skipWhitespace();
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
   * Escapes follow RFC 8259; raw control characters and unpaired
   * surrogate halves are rejected.
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

  /** Parses one escape sequence; the caller has consumed the '\'. */
  private parseEscape(): string {
    if (this.pos + 1 >= this.text.length) {
      this.fail('unterminated escape at position ' + this.pos);
    }
    const e: string = this.text[this.pos + 1];
    this.pos += 2;
    if (e === '"') {
      return '"';
    }
    if (e === '\\') {
      return '\\';
    }
    if (e === '/') {
      return '/';
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
    if (e === 'u') {
      const escapeStart: number = this.pos - 2;
      const code: number = this.readHex4(escapeStart);
      if (code >= 0xd800 && code <= 0xdbff) {
        // A high surrogate is half a code point: it must be completed by
        // an immediately following \u escape spelling the low half.
        const followedByEscape: boolean = this.pos + 2 <= this.text.length &&
          this.text[this.pos] === '\\' && this.text[this.pos + 1] === 'u';
        if (!followedByEscape) {
          this.fail('lone high surrogate at position ' + escapeStart);
        }
        const lowStart: number = this.pos;
        this.pos += 2;
        const low: number = this.readHex4(lowStart);
        if (low < 0xdc00 || low > 0xdfff) {
          this.fail('high surrogate followed by a non-low surrogate at position ' + escapeStart);
        }
        return String.fromCharCode(code, low);
      }
      if (code >= 0xdc00 && code <= 0xdfff) {
        this.fail('lone low surrogate at position ' + escapeStart);
      }
      return String.fromCharCode(code);
    }
    this.fail('invalid escape character \'' + e + '\' at position ' + (this.pos - 2));
  }

  /** Reads the four hex digits of a \u escape; escapeStart is the error position. */
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
        v = d.charCodeAt(0) - 55;
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
    // -0 reads as 0: the canonical form spells both zeros "0", and Java's
    // long has no negative zero.
    if (value === 0) {
      return 0;
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

  /** Skips JSON whitespace (space, tab, newline, carriage return). */
  private skipWhitespace(): void {
    while (this.pos < this.text.length) {
      const c: string = this.text[this.pos];
      if (c === ' ' || c === '\t' || c === '\n' || c === '\r') {
        this.pos++;
      } else {
        break;
      }
    }
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
  private fail(detail: string): void {
    a4pThrow(A4pErrorCode.A4P_DESERIALIZE_FAIL, detail);
  }
}
