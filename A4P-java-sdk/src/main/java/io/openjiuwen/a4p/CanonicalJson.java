package io.openjiuwen.a4p;

import com.fasterxml.jackson.core.JsonFactory;
import com.fasterxml.jackson.core.StreamReadFeature;
import com.fasterxml.jackson.databind.DeserializationFeature;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.MapperFeature;
import com.fasterxml.jackson.databind.json.JsonMapper;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.JsonNodeFactory;
import com.fasterxml.jackson.databind.node.LongNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import java.lang.reflect.RecordComponent;
import java.math.BigDecimal;
import java.nio.ByteBuffer;
import java.nio.CharBuffer;
import java.nio.charset.CharacterCodingException;
import java.nio.charset.CodingErrorAction;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Base64;
import java.util.Collections;
import java.util.IdentityHashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;
import java.util.stream.Collectors;

import static io.openjiuwen.a4p.A4pErrorCode.*;

/**
 * Implements deterministic protocol JSON and signing-byte normalization.
 * Object keys are sorted, array order is preserved, and insignificant whitespace is omitted.
 * Only safe integer numbers are supported; strings retain their Unicode data.
 * Parsing rejects duplicate keys and trailing tokens. Signing removes only the outer sign field.
 */
final class CanonicalJson {
    /** Largest integer exactly representable by JavaScript protocol peers. */
    public static final long MAX_SAFE_INTEGER = 9_007_199_254_740_991L;
    /** Strict shared mapper used for protocol JSON parsing and conversion. */
    static final JsonMapper MAPPER = JsonMapper.builder(JsonFactory.builder()
            .enable(StreamReadFeature.STRICT_DUPLICATE_DETECTION).build())
            .disable(MapperFeature.ALLOW_COERCION_OF_SCALARS)
            .disable(DeserializationFeature.ACCEPT_FLOAT_AS_INT)
            .enable(DeserializationFeature.FAIL_ON_NUMBERS_FOR_ENUMS)
            .enable(DeserializationFeature.FAIL_ON_TRAILING_TOKENS)
            .enable(DeserializationFeature.USE_BIG_DECIMAL_FOR_FLOATS)
            .build();

    /**
     * Prevents instantiation of this utility class.
     */
    private CanonicalJson() {}

    /**
     * Serializes a supported protocol value as deterministic compact JSON with sorted object keys.
     * @param value value to validate or normalize
     * @return compact canonical JSON
     */
    public static String stringify(Object value) {
        try {
            return render(normalize(value, new IdentityHashMap<>(), 0));
        } catch (A4pException error) {
            throw error;
        } catch (Exception error) {
            throw new A4pException(A4P_SERIALIZE_FAIL);
        }
    }

    /**
     * Returns canonical UTF-8 bytes after removing only the outer sign field.
     * Embedded intent signatures remain covered by the enclosing signature.
     * @param intent protocol intent to process
     * @return canonical UTF-8 bytes excluding only the outer sign field
     */
    public static byte[] toSignableBytes(Object intent) {
        Object normalized = normalize(intent, new IdentityHashMap<>(), 0);
        if (!(normalized instanceof Map<?, ?> fields)) {
            throw new A4pException(A4P_SERIALIZE_FAIL);
        }
        Map<Object, Object> unsigned = new LinkedHashMap<>(fields);
        unsigned.remove("sign");
        return encodeUtf8(stringify(unsigned));
    }

    /**
     * Encodes bytes as unpadded Base64URL.
     * @param data bytes to encode
     * @return unpadded Base64URL text
     */
    public static String base64UrlEncode(byte[] data) {
        return Base64.getUrlEncoder().withoutPadding().encodeToString(data);
    }

    /**
     * Decodes canonical unpadded Base64URL and rejects noncanonical or invalid encodings.
     * @param data canonical unpadded Base64URL text
     * @return decoded bytes
     */
    public static byte[] base64UrlDecode(String data) {
        try {
            if (data == null || !data.matches("[A-Za-z0-9_-]*")) {
                throw new IllegalArgumentException();
            }
            byte[] decoded = Base64.getUrlDecoder().decode(data);
            if (!base64UrlEncode(decoded).equals(data)) {
                throw new IllegalArgumentException();
            }
            return decoded;
        } catch (IllegalArgumentException error) {
            throw new A4pException(A4P_DESERIALIZE_FAIL);
        }
    }

    /**
     * Encodes a string as UTF-8, rejecting malformed surrogate sequences.
     * @param data Unicode string to encode
     * @return strict UTF-8 encoding
     */
    public static byte[] encodeUtf8(String data) {
        try {
            ByteBuffer encoded = StandardCharsets.UTF_8.newEncoder()
                    .onMalformedInput(CodingErrorAction.REPORT).encode(CharBuffer.wrap(data));
            byte[] bytes = new byte[encoded.remaining()];
            encoded.get(bytes);
            return bytes;
        } catch (CharacterCodingException | NullPointerException error) {
            throw new A4pException(A4P_SERIALIZE_FAIL);
        }
    }

    /**
     * Decodes UTF-8 strictly and rejects malformed byte sequences.
     * @param data UTF-8 bytes to decode
     * @return decoded Unicode string
     */
    public static String decodeUtf8(byte[] data) {
        try {
            return StandardCharsets.UTF_8.newDecoder().onMalformedInput(CodingErrorAction.REPORT)
                    .decode(ByteBuffer.wrap(data)).toString();
        } catch (CharacterCodingException | NullPointerException error) {
            throw new A4pException(A4P_DESERIALIZE_FAIL);
        }
    }

    /**
     * Parses JSON with duplicate-key and trailing-token detection.
     * @param json JSON representation to parse
     * @return decoded or parsed value
     */
    static Object parse(String json) {
        try {
            return MAPPER.readValue(json, Object.class);
        } catch (Exception error) {
            throw new A4pException(A4P_DESERIALIZE_FAIL);
        }
    }

    /**
     * Creates a recursively normalized immutable snapshot of a nullable map.
     * @param source map to snapshot, or null
     * @return immutable normalized map, or null
     * @throws A4pException if a value cannot be represented by canonical protocol JSON
     */
    @SuppressWarnings("unchecked")
    static Map<String, Object> freezeMap(Map<String, Object> source) {
        return source == null ? null : (Map<String, Object>) normalize(source, new IdentityHashMap<>(), 0);
    }

    // Deep copy that stores every integral number as a long, so equal payloads compare equal after a round trip.
    /**
     * Copies a JSON tree defensively and normalizes integral nodes representable as long.
     * @param node JSON tree to inspect or decode
     * @return defensive JSON copy, or null
     */
    static JsonNode freezeNode(JsonNode node) {
        if (node == null) { return null; }
        if (node.isIntegralNumber() && node.canConvertToLong()) { return LongNode.valueOf(node.longValue()); }
        if (node.isObject()) {
            ObjectNode copy = JsonNodeFactory.instance.objectNode();
            node.properties().forEach(entry -> copy.set(entry.getKey(), freezeNode(entry.getValue())));
            return copy;
        }
        if (node.isArray()) {
            ArrayNode copy = JsonNodeFactory.instance.arrayNode();
            node.forEach(item -> copy.add(freezeNode(item)));
            return copy;
        }
        return node.deepCopy();
    }

    /**
     * Converts a JSON tree to normalized protocol scalars and immutable containers.
     * @param node JSON tree to inspect or decode
     * @param ancestors identity set used to detect reference cycles
     * @param depth current normalization nesting depth
     * @return normalized scalar or immutable container
     */
    private static Object normalizeNode(JsonNode node, IdentityHashMap<Object, Boolean> ancestors, int depth) {
        if (node.isNull()) { return null; }
        if (node.isTextual()) { return node.textValue(); }
        if (node.isBoolean()) { return node.booleanValue(); }
        if (node.isNumber()) { return normalize(node.numberValue(), ancestors, depth); }
        if (node.isObject()) {
            Map<String, Object> result = new TreeMap<>();
            node.properties().forEach(entry ->
                    result.put(entry.getKey(), normalize(entry.getValue(), ancestors, depth + 1)));
            return Collections.unmodifiableMap(result);
        }
        if (node.isArray()) {
            List<Object> result = new ArrayList<>();
            node.forEach(item -> result.add(normalize(item, ancestors, depth + 1)));
            return Collections.unmodifiableList(result);
        }
        throw new A4pException(A4P_SERIALIZE_FAIL);
    }

    /**
     * Normalizes safe integers, protocol records and supported containers.
     * Rejects unsupported types, cyclic object graphs and nesting deeper than 100.
     * @param value value to validate or normalize
     * @param ancestors identity set used to detect reference cycles
     * @param depth current normalization nesting depth
     * @return normalized protocol value
     */
    private static Object normalize(Object value, IdentityHashMap<Object, Boolean> ancestors, int depth) {
        if (depth > 100) {
            throw new A4pException(A4P_SERIALIZE_FAIL);
        }
        if (value == null || value instanceof String || value instanceof Boolean) {
            return value;
        }
        if (value instanceof Number number) {
            try {
                long integer = new BigDecimal(number.toString()).longValueExact();
                if (integer < -MAX_SAFE_INTEGER || integer > MAX_SAFE_INTEGER) {
                    throw new ArithmeticException();
                }
                return integer;
            } catch (ArithmeticException | NumberFormatException error) {
                throw new A4pException(A4P_SERIALIZE_FAIL);
            }
        }
        if (value instanceof Enum<?> constant && constant.getDeclaringClass().getEnclosingClass() == Models.class) {
            return constant.name();
        }
        if (value instanceof JsonNode node) {
            return normalizeNode(node, ancestors, depth);
        }
        if (ancestors.put(value, true) != null) {
            throw new A4pException(A4P_SERIALIZE_FAIL);
        }
        try {
            if (value instanceof Map<?, ?> source) {
                Map<String, Object> result = new TreeMap<>();
                for (Map.Entry<?, ?> entry : source.entrySet()) {
                    if (!(entry.getKey() instanceof String key)) {
                        throw new A4pException(A4P_SERIALIZE_FAIL);
                    }
                    result.put(key, normalize(entry.getValue(), ancestors, depth + 1));
                }
                return Collections.unmodifiableMap(result);
            }
            if (value instanceof List<?> source) {
                List<Object> result = new ArrayList<>();
                for (Object item : source) {
                    result.add(normalize(item, ancestors, depth + 1));
                }
                return Collections.unmodifiableList(result);
            }
            if (value.getClass().isRecord() && value.getClass().getEnclosingClass() == Models.class) {
                Map<String, Object> fields = new TreeMap<>();
                for (RecordComponent component : value.getClass().getRecordComponents()) {
                    Object field = component.getAccessor().invoke(value);
                    if (field != null) {
                        fields.put(component.getName(), normalize(field, ancestors, depth + 1));
                    }
                }
                return Collections.unmodifiableMap(fields);
            }
            throw new A4pException(A4P_SERIALIZE_FAIL);
        } catch (ReflectiveOperationException error) {
            throw new A4pException(A4P_SERIALIZE_FAIL);
        } finally {
            ancestors.remove(value);
        }
    }

    /**
     * Renders a normalized value as compact deterministic JSON.
     * @param value value to validate or normalize
     * @return compact JSON representation
     */
    private static String render(Object value) {
        if (value == null) { return "null"; }
        if (value instanceof String text) { return quote(text); }
        if (value instanceof Map<?, ?> fields) {
            return fields.entrySet().stream()
                    .map(entry -> quote((String) entry.getKey()) + ":" + render(entry.getValue()))
                    .collect(Collectors.joining(",", "{", "}"));
        }
        if (value instanceof List<?> items) {
            return items.stream().map(CanonicalJson::render).collect(Collectors.joining(",", "[", "]"));
        }
        return value.toString();
    }

    /**
     * Quotes a valid Unicode string using JSON escapes without Unicode normalization.
     * @param text string to quote
     * @return JSON string literal
     */
    private static String quote(String text) {
        encodeUtf8(text);
        StringBuilder result = new StringBuilder("\"");
        for (int index = 0; index < text.length(); index++) {
            char character = text.charAt(index);
            switch (character) {
                case '"' -> result.append("\\\"");
                case '\\' -> result.append("\\\\");
                case '\b' -> result.append("\\b");
                case '\f' -> result.append("\\f");
                case '\n' -> result.append("\\n");
                case '\r' -> result.append("\\r");
                case '\t' -> result.append("\\t");
                default -> {
                    if (character < 0x20) {
                        result.append(String.format("\\u%04x", (int) character));
                    } else {
                        result.append(character);
                    }
                }
            }
        }
        return result.append('"').toString();
    }
}
