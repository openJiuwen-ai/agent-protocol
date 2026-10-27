package io.openjiuwen.a4p;

import com.fasterxml.jackson.databind.node.JsonNodeFactory;
import com.fasterxml.jackson.databind.node.ObjectNode;
import io.openjiuwen.a4p.Models.*;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.ValueSource;
import static io.openjiuwen.a4p.A4pErrorCode.*;
import static io.openjiuwen.a4p.Fixtures.*;
import static org.junit.jupiter.api.Assertions.*;

class CodecTest {
    private final A4pIntentCodec codec = A4pIntentCodec.standard();

    @Test
    void canonicalVectorsMatchArkts() {
        assertEquals("{\"a\":2,\"b\":1}", CanonicalJson.stringify(Map.of("b", 1, "a", 2)));
        assertEquals("[3,1,2]", CanonicalJson.stringify(List.of(3, 1, 2)));
        assertEquals("\"a\\\"b\\\\c\\nd\\te\"", CanonicalJson.stringify("a\"b\\c\nd\te"));
        assertEquals("\"x\\u0001y\"", CanonicalJson.stringify("x\u0001y"));
        assertEquals("{\"中\":\"文\"}", CanonicalJson.stringify(Map.of("中", "文")));
        assertEquals("9007199254740991", CanonicalJson.stringify(CanonicalJson.MAX_SAFE_INTEGER));
        assertEquals("-9007199254740991", CanonicalJson.stringify(-CanonicalJson.MAX_SAFE_INTEGER));
        assertEquals("0", CanonicalJson.stringify(-0.0));
        assertEquals("{\"10\":1,\"2\":2}", CanonicalJson.stringify(Map.of("2", 2, "10", 1)));
        ObjectNode node = JsonNodeFactory.instance.objectNode().put("b", 1).put("a", "x");
        assertEquals("{\"a\":\"x\",\"b\":1}", CanonicalJson.stringify(node));
    }

    @Test
    void enumsAreSerializedByName() {
        assertEquals("\"AI_PAY\"", CanonicalJson.stringify(IntentActionType.AI_PAY));
        assertEquals("\"CONSUMED\"", CanonicalJson.stringify(IntentExecutionStatus.CONSUMED));
        assertEquals("\"PENDING\"", CanonicalJson.stringify(ExecutionStatus.PENDING));
        assertEquals("\"RSA3072\"", CanonicalJson.stringify(AlgorithmType.RSA3072));
    }

    @Test
    void rejectsNonCanonicalNumbersCyclesAndObjects() {
        for (Number number : List.of(1.5, Double.NaN, Double.POSITIVE_INFINITY, 9_007_199_254_740_992L)) {
            thrown(A4P_SERIALIZE_FAIL, () -> CanonicalJson.stringify(number));
        }
        thrown(A4P_SERIALIZE_FAIL, () -> CanonicalJson.stringify(new Object()));
        thrown(A4P_SERIALIZE_FAIL, () -> CanonicalJson.stringify(JsonNodeFactory.instance.numberNode(1.5)));
        List<Object> cycle = new ArrayList<>();
        cycle.add(cycle);
        thrown(A4P_SERIALIZE_FAIL, () -> CanonicalJson.stringify(cycle));
        Map<String, Object> nullable = new LinkedHashMap<>();
        nullable.put("null", null);
        assertEquals("{\"null\":null}", CanonicalJson.stringify(nullable));
    }

    @Test
    void stripsOnlyTopLevelSignAndKeepsNestedSignatures() {
        Map<String, Object> value = Map.of("sign", Map.of("alg", "RSA3072", "sign", "outer"),
                "nested", Map.of("sign", "inner"));
        assertEquals("{\"nested\":{\"sign\":\"inner\"}}",
                new String(CanonicalJson.toSignableBytes(value), StandardCharsets.UTF_8));
        IntentL3 intent = authorized(constraints());
        assertTrue(new String(CanonicalJson.toSignableBytes(intent), StandardCharsets.UTF_8)
                .contains(intent.intentL2().sign().sign()));
    }

    @Test
    void algAndKeyIdentifierAreDetachedFromTheirOwnLevel() {
        IntentL3 intent = authorized(constraints());
        IntentL3 relabeled = new IntentL3(intent.version(), intent.intentL2(), intent.signingEnv(), intent.timestamp(),
                new SignatureInfo(AlgorithmType.RSA3072, "other-key", intent.sign().sign()));
        assertArrayEquals(CanonicalJson.toSignableBytes(intent), CanonicalJson.toSignableBytes(relabeled));
        String signed = new String(CanonicalJson.toSignableBytes(intent), StandardCharsets.UTF_8);
        assertTrue(signed.contains("\"keyIdentifier\":\"key\""));
    }

    @ParameterizedTest
    @ValueSource(strings = {"A", "YQ==", "YR", "YWJ", "a+b", "a/b", "a b"})
    void rejectsInvalidBase64Url(String value) {
        thrown(A4P_DESERIALIZE_FAIL, () -> CanonicalJson.base64UrlDecode(value));
    }

    @Test
    void base64AndUtf8RoundTripStrictly() {
        byte[] bytes = new byte[256];
        for (int index = 0; index < bytes.length; index++) { bytes[index] = (byte) index; }
        assertArrayEquals(bytes, CanonicalJson.base64UrlDecode(CanonicalJson.base64UrlEncode(bytes)));
        assertEquals("YQ", CanonicalJson.base64UrlEncode(new byte[]{97}));
        assertArrayEquals(new byte[0], CanonicalJson.base64UrlDecode(""));
        String unicode = "中文 😀";
        assertEquals(unicode, CanonicalJson.decodeUtf8(CanonicalJson.encodeUtf8(unicode)));
        thrown(A4P_SERIALIZE_FAIL, () -> CanonicalJson.encodeUtf8("\ud800"));
        thrown(A4P_SERIALIZE_FAIL, () -> CanonicalJson.stringify("\udc00"));
        thrown(A4P_DESERIALIZE_FAIL, () -> CanonicalJson.decodeUtf8(new byte[]{(byte) 0xc0, (byte) 0x80}));
        thrown(A4P_DESERIALIZE_FAIL, () -> CanonicalJson.decodeUtf8(new byte[]{(byte) 0xf0, (byte) 0x80}));
    }

    @Test
    void wireFormatUsesSpecificationNamesAndStringEnums() {
        IntentL3 intent = authorized(constraints());
        String json = codec.serializeIntentL3(intent);
        assertTrue(json.contains("\"intentConversation\":\"conversation\""));
        assertTrue(json.contains("\"resources\":{\"itemsWhiteList\""));
        assertTrue(json.contains("\"action\":\"AI_PAY\""));
        assertTrue(json.contains("\"userIdentity\":{\"id\":\"user\",\"identityProvider\":\"idp\"}"));
        assertFalse(json.contains("intentConversion"));
        assertFalse(json.contains("\"uid\""));
        assertFalse(json.contains("\"resource\":"));
        assertFalse(json.contains("null"), "absent optional fields are omitted from signed content");
    }

    @Test
    void codecRoundTripsAllLevelsWithoutChangingSignedBytes() {
        IntentL3 intent = authorized(constraints());
        IntentL3 decoded = ok(codec.deserializeIntentL3(codec.serializeIntentL3(intent)));
        assertEquals(intent, decoded);
        assertArrayEquals(CanonicalJson.toSignableBytes(intent), CanonicalJson.toSignableBytes(decoded));
        assertEquals(intent.intentL2(), ok(codec.deserializeIntentL2(codec.serializeIntentL2(intent.intentL2()))));
        assertEquals(intent.intentL2().intentL1(), ok(codec.deserializeIntentL1(
                codec.serializeIntentL1(intent.intentL2().intentL1()))));
        assertTrue(ok(A4p.verifyIntentAuthenticity(decoded, VERIFIER)).valid());
        ok(A4p.activateIntent(decoded, VERIFIER, NOW));
        thrown(A4P_INVALID_PARAMETER, () -> codec.serializeIntentL1(null));
    }

    @Test
    void malformedAndUntrustedJsonIsRejected() {
        fails(A4P_INVALID_PARAMETER, codec.deserializeIntentL1(""));
        fails(A4P_INVALID_PARAMETER, codec.deserializeIntentL1(null));
        fails(A4P_DESERIALIZE_FAIL, codec.deserializeIntentL1("{"));
        fails(A4P_INVALID_PARAMETER, codec.deserializeIntentL1("null"));
        fails(A4P_INVALID_PARAMETER, codec.deserializeIntentL1("{}"));
        fails(A4P_DESERIALIZE_FAIL, codec.deserializeIntentL1("{} {}"));
        fails(A4P_DESERIALIZE_FAIL, codec.deserializeIntentL1("{\"version\":1,\"version\":2}"));
        String json = codec.serializeIntentL1(authorized(constraints()).intentL2().intentL1());
        List<String> tampered = List.of(
                json.replace("\"timestamp\":10000", "\"timestamp\":1.5"),
                json.replace("\"timestamp\":10000", "\"timestamp\":\"10000\""),
                json.replace("\"action\":\"AI_PAY\"", "\"action\":\"AI_REFUND\""),
                json.replace("\"action\":\"AI_PAY\"", "\"action\":0"),
                json.replace("\"version\":\"1.0\"", "\"version\":\"1.0\",\"extra\":1"),
                json.replace("\"intentConversation\"", "\"intentConversion\""),
                json.replace("\"resources\"", "\"resource\""),
                json.replace("\"totalMaxAmount\":1000", "\"totalMaxAmount\":1000.5"),
                json.replace("\"totalMaxAmount\":1000", "\"totalMaxAmount\":null"),
                // An explicit null on an optional field is rejected rather than read as "absent".
                json.replace("\"singleOrderMaxAmount\":500", "\"singleOrderMaxAmount\":null"),
                json.replace("\"quantity\":5", "\"quantity\":5,\"extra\":true"));
        for (String input : tampered) {
            assertNotEquals(json, input);
            fails(A4P_INVALID_PARAMETER, codec.deserializeIntentL1(input));
        }
    }

    // Completed in period 1, then canceled and pending in period 2.
    private static IntentExecutionContext ledgerContext() {
        IntentExecutionContext context = context();
        ok(A4p.createExecution(context, execution("one", 100, 1), NOW));
        ok(A4p.completeExecution(context, "one", NOW));
        ok(A4p.createExecution(context, execution("two", 100, 1), NOW + 10_000));
        ok(A4p.cancelExecution(context, "two", NOW + 10_000));
        ok(A4p.createExecution(context, execution("three", 100, 1), NOW + 10_000));
        return context;
    }

    @Test
    void executionContextRoundTripsAndKeepsEnforcingTheLedger() {
        IntentExecutionContext context = ledgerContext();
        String json = codec.serializeIntentExecutionContext(context);
        assertTrue(json.contains("\"executionInfo\":{\"leftAmount\":800,\"leftExecutionCount\":1,"
                + "\"leftPeriodExecutionCount\":1,\"periodIndex\":2}"));
        IntentExecutionContext restored = ok(codec.deserializeIntentExecutionContext(json));
        assertEquals(json, codec.serializeIntentExecutionContext(restored));
        assertEquals(context.intentId(), restored.intentId());
        assertEquals(context.authorizedIntent(), restored.authorizedIntent());
        assertEquals(context.status(), restored.status());
        assertEquals(context.executions(), restored.executions());
        assertEquals(info(context), info(restored));
        fails(A4P_DUPLICATE_ID, A4p.createExecution(restored, execution("one", 100, 1), NOW + 10_000));
        fails(A4P_INVALID_STATUS, A4p.completeExecution(restored, "two", NOW + 10_000));
        ok(A4p.createExecution(restored, execution("four", 100, 1), NOW + 10_000));
        fails(A4P_EXECUTION_COUNT_EXCEEDED, A4p.createExecution(restored, execution("five", 100, 1), NOW + 10_000));
        ok(A4p.completeExecution(restored, "three", NOW + 10_000));
        thrown(A4P_INVALID_PARAMETER, () -> codec.serializeIntentExecutionContext(null));
    }

    @Test
    void executionContextKeepsTerminalStatusAndDeclaresInapplicableFiguresAsNull() {
        PaymentConstraints unbounded = new PaymentConstraints(null, 1000L, "CNY", 0L, 100_000L, null, null, null, null);
        IntentExecutionContext context = ok(A4p.activateIntent(authorized(unbounded), VERIFIER, NOW));
        ok(A4p.revokeIntent(context));
        String json = codec.serializeIntentExecutionContext(context);
        assertTrue(json.contains("\"executionInfo\":{\"leftAmount\":1000,\"leftExecutionCount\":null,"
                + "\"leftPeriodExecutionCount\":null,\"periodIndex\":null},\"executions\":[]"));
        IntentExecutionContext restored = ok(codec.deserializeIntentExecutionContext(json));
        assertEquals(IntentExecutionStatus.REVOKED, restored.status());
        fails(A4P_INVALID_STATUS, A4p.createExecution(restored, execution("one", 100, 1), NOW));
        // Without a period constraint there is no current period.
        fails(A4P_INVALID_PARAMETER, codec.deserializeIntentExecutionContext(
                json.replace("\"periodIndex\":null", "\"periodIndex\":0")));
    }

    @Test
    void tamperedExecutionContextIsRejected() {
        fails(A4P_INVALID_PARAMETER, codec.deserializeIntentExecutionContext(null));
        fails(A4P_INVALID_PARAMETER, codec.deserializeIntentExecutionContext(""));
        fails(A4P_DESERIALIZE_FAIL, codec.deserializeIntentExecutionContext("{"));
        fails(A4P_INVALID_PARAMETER, codec.deserializeIntentExecutionContext("null"));
        fails(A4P_INVALID_PARAMETER, codec.deserializeIntentExecutionContext("{}"));
        String json = codec.serializeIntentExecutionContext(ledgerContext());
        String version = "\"version\":\"1.0\"}";
        assertTrue(json.endsWith(version));
        fails(A4P_UNSUPPORTED_VERSION, codec.deserializeIntentExecutionContext(
                json.substring(0, json.length() - version.length()) + "\"version\":\"2.0\"}"));
        Map<String, A4pErrorCode> tampered = new LinkedHashMap<>();
        tampered.put(json.replaceFirst("\\{", "{\"extra\":1,"), A4P_INVALID_PARAMETER);
        tampered.put(json.replace("\"intentId\":\"intent\",\"status\"", "\"intentId\":\"other\",\"status\""),
                A4P_INVALID_PARAMETER);
        tampered.put(json.replace("\"status\":\"ACTIVE\"", "\"status\":\"PAUSED\""), A4P_INVALID_PARAMETER);
        tampered.put(json.replace("\"leftAmount\":800", "\"leftAmount\":900"), A4P_INVALID_PARAMETER);
        tampered.put(json.replace("\"periodIndex\":2},\"executions\"", "\"periodIndex\":3},\"executions\""),
                A4P_INVALID_PARAMETER);
        tampered.put(json.replace("\"periodIndex\":2},\"executions\"", "\"periodIndex\":null},\"executions\""),
                A4P_INVALID_PARAMETER);
        tampered.put(json.replace("\"periodIndex\":1,", "\"periodIndex\":2,"), A4P_INVALID_PARAMETER);
        tampered.put(json.replace("\"reservedAt\":10000,", ""), A4P_INVALID_PARAMETER);
        tampered.put(json.replace("\"reservedAt\":10000,", "\"reservedAt\":\"10000\","), A4P_INVALID_PARAMETER);
        tampered.put(json.replace("\"canceledAt\":20000", "\"completedAt\":20000"), A4P_INVALID_PARAMETER);
        tampered.put(json.replace("\"status\":\"CANCELED\"", "\"status\":\"COMPLETED\""), A4P_INVALID_PARAMETER);
        tampered.put(json.replace("\"currency\":\"CNY\",\"orderId\"", "\"currency\":\"USD\",\"orderId\""),
                A4P_INVALID_PARAMETER);
        tampered.put(json.replace("\"three\"", "\"two\""), A4P_DUPLICATE_ID);
        tampered.put(json.replace("\"amount\":100", "\"amount\":501"), A4P_SINGLE_ORDER_AMOUNT_EXCEEDED);
        tampered.put(json.replace("\"quantity\":1", "\"quantity\":3"), A4P_PRODUCT_QUANTITY_EXCEEDED);
        tampered.forEach((input, code) -> {
            assertNotEquals(json, input);
            fails(code, codec.deserializeIntentExecutionContext(input));
        });
    }

    // The restored status and record timestamps must be ones the execution operations could have produced.
    @Test
    void executionContextStatusAndRecordTimestampsMustBeReachable() {
        PaymentConstraints late = new PaymentConstraints(null, 200L, "CNY", 20_000L, 100_000L, 2L, null, null, null);
        IntentExecutionContext context = ok(A4p.activateIntent(authorized(late), VERIFIER, NOW));
        ok(A4p.createExecution(context, execution("one", 100, 1), 20_000));
        String pending = codec.serializeIntentExecutionContext(context);
        fails(A4P_INVALID_PARAMETER, codec.deserializeIntentExecutionContext(
                pending.replace("\"status\":\"ACTIVE\"", "\"status\":\"CONSUMED\"")));
        fails(A4P_TIMESTAMP_TOO_EARLY, codec.deserializeIntentExecutionContext(
                pending.replace("\"reservedAt\":20000", "\"reservedAt\":19999")));
        fails(A4P_TIMESTAMP_EXPIRED, codec.deserializeIntentExecutionContext(
                pending.replace("\"reservedAt\":20000", "\"reservedAt\":100001")));

        ok(A4p.completeExecution(context, "one", 20_000));
        String completed = codec.serializeIntentExecutionContext(context);
        fails(A4P_INVALID_PARAMETER, codec.deserializeIntentExecutionContext(
                completed.replace("\"completedAt\":20000", "\"completedAt\":19999")));
        fails(A4P_INVALID_PARAMETER, codec.deserializeIntentExecutionContext(
                completed.replace("\"status\":\"ACTIVE\"", "\"status\":\"CONSUMED\"")));

        ok(A4p.createExecution(context, execution("two", 100, 1), 20_000));
        ok(A4p.cancelExecution(context, "two", 20_000));
        String canceled = codec.serializeIntentExecutionContext(context);
        fails(A4P_INVALID_PARAMETER, codec.deserializeIntentExecutionContext(
                canceled.replace("\"canceledAt\":20000", "\"canceledAt\":19999")));

        ok(A4p.createExecution(context, execution("three", 100, 1), 20_000));
        ok(A4p.completeExecution(context, "three", 20_000));
        assertEquals(IntentExecutionStatus.CONSUMED, context.status());
        String consumed = codec.serializeIntentExecutionContext(context);
        assertEquals(IntentExecutionStatus.CONSUMED, ok(codec.deserializeIntentExecutionContext(consumed)).status());
        fails(A4P_INVALID_PARAMETER, codec.deserializeIntentExecutionContext(
                consumed.replace("\"status\":\"CONSUMED\"", "\"status\":\"ACTIVE\"")));
        // REVOKED and EXPIRED are reachable whatever the ledger holds.
        assertEquals(IntentExecutionStatus.REVOKED, ok(codec.deserializeIntentExecutionContext(
                consumed.replace("\"status\":\"CONSUMED\"", "\"status\":\"REVOKED\""))).status());
    }
}
