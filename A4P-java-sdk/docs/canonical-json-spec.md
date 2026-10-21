# Canonical JSON and signed bytes

Reference: PR #130

1. Object keys are sorted lexicographically by UTF-16 code units (Java
   `String.compareTo`, equivalent to JavaScript's default string sort and to RFC 8785).
2. Array order is preserved. There is no whitespace between tokens.
3. Numbers must be finite integers in `[-9007199254740991, 9007199254740991]`.
   They use decimal notation without a fractional part or exponent. Negative zero
   becomes `0`. Amounts and timestamps are subject to additional model validation.
   Every protocol number is an integer by design; a value that is not an integer must
   be carried as a string.
4. Quotes, backslashes and control characters use JSON escapes. Characters below
   U+0020 without a short escape use lowercase `\u00xx`; other Unicode characters
   are emitted directly, with no normalization. Unpaired surrogates are rejected.
5. Java record fields with `null` represent absent optional properties and are
   omitted. Null values inside explicit JSON maps and lists remain `null`. Enums are
   rendered by name, for example `"AI_PAY"`.
6. Signing removes only the complete top-level property named `sign`, canonicalizes
   the remaining object, and encodes it as UTF-8 without a byte-order mark. It does
   not recursively remove nested signatures. The level's own `alg` and `keyIdentifier`
   are therefore not part of that level's signed bytes; the next level's signature
   covers them. This is the specified design (API specification §4.6.2).
7. Signature bytes use RFC 4648 Base64URL without padding. Decoders reject padding,
   non-URL-safe characters and nonzero unused trailing bits.

Examples:

```text
{"b":1,"a":2}                 -> {"a":2,"b":1}
[3,1,2]                       -> [3,1,2]
{"10":1,"2":2}                -> {"10":1,"2":2}
9007199254740991               -> 9007199254740991
{"sign":"outer","nested":{"sign":"inner"}}
signed bytes                  -> {"nested":{"sign":"inner"}}
```

`CanonicalJson.stringify` accepts JSON maps/lists/primitives, Jackson `JsonNode` trees
and SDK model records and enums. Arbitrary Java objects, non-string map keys, cycles and
excessive nesting fail with `A4P_SERIALIZE_FAIL`. Deserialization requires an object of
the expected model, rejects unknown properties, numeric enum values and explicit nulls
inside AI_PAY payloads, and validates every nested signature and constraint.
