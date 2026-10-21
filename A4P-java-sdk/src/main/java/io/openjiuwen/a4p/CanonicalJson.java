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

public final class CanonicalJson {
    public static final long MAX_SAFE_INTEGER = 9_007_199_254_740_991L;
    static final JsonMapper MAPPER = JsonMapper.builder(JsonFactory.builder()
            .enable(StreamReadFeature.STRICT_DUPLICATE_DETECTION).build())
            .disable(MapperFeature.ALLOW_COERCION_OF_SCALARS)
            .disable(DeserializationFeature.ACCEPT_FLOAT_AS_INT)
            .enable(DeserializationFeature.FAIL_ON_NUMBERS_FOR_ENUMS)
            .enable(DeserializationFeature.FAIL_ON_TRAILING_TOKENS)
            .enable(DeserializationFeature.USE_BIG_DECIMAL_FOR_FLOATS)
            .build();

    private CanonicalJson() {}

    public static String stringify(Object value) {
        try {
            return render(normalize(value, new IdentityHashMap<>(), 0));
        } catch (A4pException error) {
            throw error;
        } catch (Exception error) {
            throw new A4pException(A4P_SERIALIZE_FAIL);
        }
    }

    public static byte[] toSignableBytes(Object intent) {
        Object normalized = normalize(intent, new IdentityHashMap<>(), 0);
        if (!(normalized instanceof Map<?, ?> fields)) {
            throw new A4pException(A4P_SERIALIZE_FAIL);
        }
        Map<Object, Object> unsigned = new LinkedHashMap<>(fields);
        unsigned.remove("sign");
        return encodeUtf8(stringify(unsigned));
    }

    public static String base64UrlEncode(byte[] data) {
        return Base64.getUrlEncoder().withoutPadding().encodeToString(data);
    }

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

    public static String decodeUtf8(byte[] data) {
        try {
            return StandardCharsets.UTF_8.newDecoder().onMalformedInput(CodingErrorAction.REPORT)
                    .decode(ByteBuffer.wrap(data)).toString();
        } catch (CharacterCodingException | NullPointerException error) {
            throw new A4pException(A4P_DESERIALIZE_FAIL);
        }
    }

    static Object parse(String json) {
        try {
            return MAPPER.readValue(json, Object.class);
        } catch (Exception error) {
            throw new A4pException(A4P_DESERIALIZE_FAIL);
        }
    }

    @SuppressWarnings("unchecked")
    static Map<String, Object> freezeMap(Map<String, Object> source) {
        return source == null ? null : (Map<String, Object>) normalize(source, new IdentityHashMap<>(), 0);
    }

    // Deep copy that stores every integral number as a long, so equal payloads compare equal after a round trip.
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
