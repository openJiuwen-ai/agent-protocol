package io.openjiuwen.a4p;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import io.openjiuwen.a4p.Models.*;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.Callable;
import java.util.concurrent.Executors;
import org.junit.jupiter.api.Test;
import static io.openjiuwen.a4p.A4pErrorCode.*;
import static io.openjiuwen.a4p.Fixtures.*;
import static org.junit.jupiter.api.Assertions.*;

class A4pTest {
    private static final Product OTHER = new Product(new SellerInfo("seller"), new SpuSkuInfo("spu", "other"));

    @Test
    void lifecycleReservesCancelsAndConsumes() {
        IntentExecutionContext context = context();
        ok(A4p.createExecution(context, execution("one", 100, 1), NOW));
        assertEquals(900, info(context).leftAmount());
        ok(A4p.cancelExecution(context, "one", NOW));
        assertEquals(1000, info(context).leftAmount());
        assertEquals(3, info(context).leftExecutionCount());
        fails(A4P_DUPLICATE_ID, A4p.createExecution(context, execution("one", 100, 1), NOW));
        for (int index = 0; index < 3; index++) {
            String id = "completed-" + index;
            ok(A4p.createExecution(context, execution(id, 100, 1), NOW + index * 10_000));
            ok(A4p.completeExecution(context, id, NOW + index * 10_000));
        }
        assertEquals(IntentExecutionStatus.CONSUMED, context.status());
        assertEquals(4, context.executions().size());
        assertEquals(700, info(context).leftAmount());
    }

    @Test
    void periodAccountingUsesReservationClockAndReleasesOldPeriodsCorrectly() {
        IntentExecutionContext context = context();
        ok(A4p.createExecution(context, execution("one", 100, 1), NOW));
        ok(A4p.createExecution(context, execution("two", 100, 1), NOW));
        fails(A4P_PERIOD_EXECUTION_COUNT_EXCEEDED, A4p.createExecution(context, execution("three", 100, 1), NOW));
        ok(A4p.createExecution(context, execution("three", 100, 1), NOW + 10_000));
        assertEquals(2, context.executions().get(2).periodIndex());
        ok(A4p.cancelExecution(context, "one", NOW + 10_000));
        assertEquals(1, info(context).leftPeriodExecutionCount());
        assertEquals(1, info(context).leftExecutionCount());
    }

    @Test
    void enforcesAmountCountCurrencyAndProductLimits() {
        IntentExecutionContext context = context();
        fails(A4P_SINGLE_ORDER_AMOUNT_EXCEEDED, A4p.createExecution(context, execution("big", 501, 1), NOW));
        fails(A4P_PRODUCT_QUANTITY_EXCEEDED, A4p.createExecution(context, execution("many", 1, 6), NOW));
        Execution foreign = PaymentProfile.execution("foreign", "1.0",
                new Order("foreign", 1L, "USD", List.of(new CartItem(PRODUCT, 1L)), NOW));
        fails(A4P_INVALID_PARAMETER, A4p.createExecution(context, foreign, NOW));
        Product unknown = new Product(new SellerInfo("other"), PRODUCT.spusku());
        fails(A4P_PRODUCT_NOT_AUTHORIZED, A4p.createExecution(context, cart("outside", 1, new CartItem(unknown, 1L)), NOW));
        ok(A4p.createExecution(context, execution("one", 500, 1), NOW));
        ok(A4p.createExecution(context, execution("two", 500, 1), NOW));
        fails(A4P_AMOUNT_EXCEEDED, A4p.createExecution(context, execution("three", 1, 1), NOW + 10_000));
    }

    @Test
    void revokedIntentCanFinalizeButCannotReserve() {
        IntentExecutionContext context = context();
        ok(A4p.createExecution(context, execution("one", 100, 1), NOW));
        ok(A4p.createExecution(context, execution("two", 100, 1), NOW));
        ok(A4p.revokeIntent(context));
        fails(A4P_INVALID_STATUS, A4p.revokeIntent(context));
        fails(A4P_INVALID_STATUS, A4p.createExecution(context, execution("three", 100, 1), NOW));
        ok(A4p.completeExecution(context, "one", NOW));
        ok(A4p.cancelExecution(context, "two", NOW));
        assertEquals(IntentExecutionStatus.REVOKED, context.status());
        fails(A4P_INVALID_STATUS, A4p.completeExecution(context, "one", NOW));
        fails(A4P_INVALID_PARAMETER, A4p.completeExecution(context, "missing", NOW));
    }

    @Test
    void finalizationCannotPrecedeTheReservation() {
        IntentExecutionContext context = context();
        ok(A4p.createExecution(context, execution("one", 100, 1), NOW));
        fails(A4P_INVALID_PARAMETER, A4p.completeExecution(context, "one", NOW - 1));
        fails(A4P_INVALID_PARAMETER, A4p.cancelExecution(context, "one", NOW - 1));
        assertEquals(ExecutionStatus.PENDING, context.executions().get(0).status());
        ok(A4p.completeExecution(context, "one", NOW));
    }

    @Test
    void terminalStatesAreNotRewrittenWhenTheLedgerDrains() {
        IntentExecutionContext revoked = context();
        for (int index = 0; index < 3; index++) {
            ok(A4p.createExecution(revoked, execution("r-" + index, 100, 1), NOW + index * 10_000));
        }
        ok(A4p.revokeIntent(revoked));
        for (int index = 0; index < 3; index++) {
            ok(A4p.completeExecution(revoked, "r-" + index, NOW + 30_000));
        }
        assertEquals(IntentExecutionStatus.REVOKED, revoked.status());

        IntentExecutionContext expired = ok(A4p.activateIntent(authorized(
                new PaymentConstraints(null, 100L, "CNY", 0L, 100_000L, 1L, null, null, null)), VERIFIER, NOW));
        ok(A4p.createExecution(expired, execution("e", 100, 1), NOW));
        fails(A4P_TIMESTAMP_EXPIRED, A4p.createExecution(expired, execution("late", 1, 1), 100_001));
        ok(A4p.completeExecution(expired, "e", 100_001));
        assertEquals(IntentExecutionStatus.EXPIRED, expired.status());
    }

    @Test
    void expirationUsesNowNotOrderTimestampAndStillAllowsFinalization() {
        IntentExecutionContext context = context();
        ok(A4p.createExecution(context, execution("one", 100, 1), 100_000));
        ok(A4p.createExecution(context, execution("two", 100, 1), 100_000));
        fails(A4P_TIMESTAMP_EXPIRED, A4p.createExecution(context, execution("three", 100, 1), 100_001));
        assertEquals(IntentExecutionStatus.EXPIRED, context.status());
        assertEquals(800, info(context).leftAmount());
        ok(A4p.cancelExecution(context, "one", 100_001));
        assertEquals(900, info(context).leftAmount());
        ok(A4p.completeExecution(context, "two", 100_001));
        assertEquals(ExecutionStatus.COMPLETED, context.executions().get(1).status());
        assertEquals(IntentExecutionStatus.EXPIRED, context.status());
        PaymentConstraints future = new PaymentConstraints(null, 1000L, "CNY", 20_000L, 30_000L,
                null, null, null, null);
        IntentExecutionContext early = ok(A4p.activateIntent(authorized(future), VERIFIER, NOW));
        fails(A4P_TIMESTAMP_TOO_EARLY, A4p.createExecution(early, execution("early", 1, 1), NOW));
        assertEquals(IntentExecutionStatus.ACTIVE, early.status());
    }

    @Test
    void noCountCapStaysActiveUntilAmountIsUsedUpAndNegativePeriodUsesFloor() {
        PaymentConstraints constraints = new PaymentConstraints(null, 1000L, "CNY", 20_000L, 100_000L,
                null, 20_000L, 10L, 5L);
        IntentExecutionContext context = ok(A4p.activateIntent(authorized(constraints), VERIFIER, 19_999));
        assertEquals(-1, info(context).periodIndex());
        ok(A4p.createExecution(context, execution("one", 600, 1), 20_000));
        assertEquals(0, context.executions().get(0).periodIndex());
        ok(A4p.createExecution(context, execution("two", 400, 1), 20_000));
        ok(A4p.completeExecution(context, "one", 20_000));
        assertEquals(IntentExecutionStatus.ACTIVE, context.status(), "a pending record keeps the intent open");
        ok(A4p.completeExecution(context, "two", 20_000));
        assertEquals(0, info(context).leftAmount());
        assertEquals(IntentExecutionStatus.CONSUMED, context.status());
    }

    @Test
    void cartMustMatchWhitelistGroupsOneToOneInAnyOrder() {
        CreateIntentRequest request = new CreateIntentRequest("conversation", "summary", structured(List.of(
                new ItemWhiteList(List.of(PRODUCT), 5L), new ItemWhiteList(List.of(OTHER), 5L)), constraints()), null);
        IntentExecutionContext context = activate(request);
        fails(A4P_INVALID_PARAMETER, A4p.createExecution(context, execution("short", 1, 1), NOW));
        fails(A4P_INVALID_PARAMETER, A4p.createExecution(context, cart("long", 1,
                new CartItem(PRODUCT, 1L), new CartItem(OTHER, 1L), new CartItem(PRODUCT, 1L)), NOW));
        fails(A4P_PRODUCT_NOT_AUTHORIZED, A4p.createExecution(context, cart("same-group", 1,
                new CartItem(PRODUCT, 1L), new CartItem(PRODUCT, 1L)), NOW));
        ok(A4p.createExecution(context, cart("reversed", 1, new CartItem(OTHER, 1L), new CartItem(PRODUCT, 1L)), NOW));
        ok(A4p.createExecution(context, cart("both", 1, new CartItem(PRODUCT, 1L), new CartItem(OTHER, 1L)), NOW));
        assertEquals(2, context.executions().size());
    }

    @Test
    void signaturesAndCallbackFailuresAreCheckedBeforeStateChanges() {
        IntentL3 intent = authorized(constraints());
        SignatureVerifier reject = (data, signature) -> new VerificationResult(false, A4P_VERIFY_FAIL.code(), "NO");
        assertFalse(ok(A4p.verifyIntentAuthenticity(intent, reject)).valid());
        fails(A4P_VERIFY_FAIL, A4p.activateIntent(intent, reject, NOW));
        fails(A4P_VERIFY_FAIL, A4p.signIntent(intent.intentL2(), reject, intent.signingEnv(), SIGNER, NOW));
        fails(A4P_VERIFY_FAIL, A4p.issueIntent(intent.intentL2().intentL1(), reject, issueContext(), SIGNER, NOW));
        fails(A4P_VERIFY_FAIL, A4p.activateIntent(intent, (data, sign) -> { throw new Exception("secret"); }, NOW));
        fails(A4P_VERIFY_FAIL, A4p.verifyIntentAuthenticity(intent, (data, sign) -> null));
        fails(A4P_SIGNING_FAIL, A4p.createIntent(request(constraints()), data -> { throw new Exception(); }, NOW));
        fails(A4P_SIGNING_FAIL, A4p.createIntent(request(constraints()), data -> null, NOW));
        // valid and code must agree, otherwise the callback result is not trusted either way.
        SignatureVerifier validButFailed = (data, sign) -> new VerificationResult(true, A4P_VERIFY_FAIL.code(), null);
        SignatureVerifier invalidButSucceeded = (data, sign) -> new VerificationResult(false, A4P_SUCCESS.code(), null);
        fails(A4P_VERIFY_FAIL, A4p.activateIntent(intent, validButFailed, NOW));
        fails(A4P_VERIFY_FAIL, A4p.verifyIntentAuthenticity(intent, validButFailed));
        fails(A4P_VERIFY_FAIL, A4p.verifyIntentAuthenticity(intent, invalidButSucceeded));
    }

    @Test
    void verifyIntentAuthenticityChecksTheIntentCenterSignatureOnly() {
        IntentL3 intent = authorized(constraints());
        List<byte[]> seen = new ArrayList<>();
        SignatureVerifier issuer = (data, signature) -> {
            seen.add(data);
            return VERIFIER.verify(data, signature);
        };
        assertTrue(ok(A4p.verifyIntentAuthenticity(intent, issuer)).valid());
        assertArrayEquals(CanonicalJson.toSignableBytes(intent.intentL2()), seen.get(0));

        SignatureInfo forged = new SignatureInfo(AlgorithmType.RSA3072, "key", "Zm9yZ2Vk");
        IntentL3 forgedUser = new IntentL3(intent.version(), intent.intentL2(), intent.signingEnv(),
                intent.timestamp(), forged);
        assertTrue(ok(A4p.verifyIntentAuthenticity(forgedUser, VERIFIER)).valid());
        fails(A4P_VERIFY_FAIL, A4p.activateIntent(forgedUser, VERIFIER, NOW));

        IntentL2 issued = intent.intentL2();
        IntentL2 tampered = new IntentL2(issued.version(), issued.intentL1(), new UserIdentity("idp", "mallory", null),
                issued.agentIdentity(), issued.serviceProviderIdentity(), issued.timestamp(), issued.sign());
        IntentL3 rewrapped = new IntentL3(intent.version(), tampered, intent.signingEnv(), intent.timestamp(),
                intent.sign());
        assertFalse(ok(A4p.verifyIntentAuthenticity(rewrapped, VERIFIER)).valid());
        fails(A4P_INVALID_PARAMETER, A4p.verifyIntentAuthenticity(null, VERIFIER));
    }

    @Test
    void nestedVersionAndInvalidConstraintsAreRejected() {
        IntentL3 intent = authorized(constraints());
        IntentL1 original = intent.intentL2().intentL1();
        IntentL1 bad = new IntentL1("2.0", original.intentId(), original.intentConversation(), original.intentSummary(),
                original.structuredIntent(), NOW, original.sign());
        IntentL2 second = new IntentL2("1.0", bad, intent.intentL2().userIdentity(), intent.intentL2().agentIdentity(),
                intent.intentL2().serviceProviderIdentity(), NOW, intent.intentL2().sign());
        fails(A4P_UNSUPPORTED_VERSION, A4p.activateIntent(
                new IntentL3("1.0", second, intent.signingEnv(), NOW, intent.sign()), VERIFIER, NOW));
        PaymentConstraints partial = new PaymentConstraints(null, 100L, "CNY", 0L, 100L, null, 0L, null, null);
        fails(A4P_INVALID_PARAMETER, A4p.createIntent(request(partial), SIGNER, NOW));
        fails(A4P_INVALID_PARAMETER, A4p.createIntent(null, SIGNER, NOW));
        fails(A4P_INVALID_PARAMETER, A4p.createExecution(context(), execution("zero", 0, 1), NOW));
        fails(A4P_INVALID_PARAMETER, A4p.createExecution(null, execution("no-context", 1, 1), NOW));
    }

    @Test
    void snapshotsCannotBeMutatedByCallers() {
        Map<String, Object> nested = new HashMap<>(Map.of("name", "original"));
        SigningEnv environment = new SigningEnv(Map.of("nested", nested), Map.of());
        nested.put("name", "changed");
        assertEquals("original", ((Map<?, ?>) environment.device().get("nested")).get("name"));
        assertThrows(UnsupportedOperationException.class, () -> environment.device().put("x", 1));
        IntentExecutionContext context = context();
        assertThrows(UnsupportedOperationException.class, () -> context.executions().clear());

        StructuredIntent structured = context.authorizedIntent().intentL2().intentL1().structuredIntent();
        ((ObjectNode) structured.resources()).removeAll();
        assertEquals(1, PaymentProfile.resources(structured).itemsWhiteList().size());
        ObjectNode raw = (ObjectNode) structured.resources();
        StructuredIntent built = new StructuredIntent(IntentActionType.AI_PAY, raw, structured.constraints());
        raw.removeAll();
        assertEquals(1, PaymentProfile.resources(built).itemsWhiteList().size());
        ((ObjectNode) context.executionInfo()).put("leftAmount", 0);
        assertEquals(1000, info(context).leftAmount());
    }

    @Test
    void executionInfoDeclaresEveryFieldAndUsesNullWhenNotApplicable() {
        IntentExecutionContext capped = context();
        JsonNode full = capped.executionInfo();
        assertEquals(3, full.get("leftExecutionCount").longValue());
        assertEquals(1000, full.get("leftAmount").longValue());
        assertEquals(1, full.get("periodIndex").longValue());
        assertEquals(2, full.get("leftPeriodExecutionCount").longValue());

        IntentExecutionContext open = ok(A4p.activateIntent(authorized(
                new PaymentConstraints(null, 1000L, "CNY", 0L, 100_000L, null, null, null, null)), VERIFIER, NOW));
        JsonNode sparse = open.executionInfo();
        assertEquals(4, sparse.size());
        assertTrue(sparse.get("leftExecutionCount").isNull());
        assertTrue(sparse.get("periodIndex").isNull());
        assertTrue(sparse.get("leftPeriodExecutionCount").isNull());
        assertEquals(1000, sparse.get("leftAmount").longValue());
    }

    @Test
    void overlappingWhitelistGroupsAndDuplicateCartEntriesConsumeEveryMatchingGroup() {
        CreateIntentRequest request = new CreateIntentRequest("conversation", "summary", structured(List.of(
                new ItemWhiteList(List.of(PRODUCT), 5L), new ItemWhiteList(List.of(PRODUCT), 2L)), constraints()),
                "overlap");
        IntentExecutionContext context = activate(request);
        ok(A4p.createExecution(context, cart("repeated", 1, new CartItem(PRODUCT, 1L), new CartItem(PRODUCT, 1L)), NOW));
        fails(A4P_PRODUCT_QUANTITY_EXCEEDED, A4p.createExecution(context,
                cart("excess", 1, new CartItem(PRODUCT, 1L), new CartItem(PRODUCT, 1L)), NOW));
        ok(A4p.cancelExecution(context, "repeated", NOW));
        ok(A4p.createExecution(context, cart("released", 1, new CartItem(PRODUCT, 1L), new CartItem(PRODUCT, 1L)), NOW));
        assertEquals(2, context.executions().size());
    }

    @Test
    void pendingReservationsEnforceLifetimeCountAcrossPeriods() {
        IntentExecutionContext context = context();
        for (int index = 0; index < 3; index++) {
            ok(A4p.createExecution(context, execution("pending-" + index, 1, 1), NOW + index * 10_000));
        }
        fails(A4P_EXECUTION_COUNT_EXCEEDED, A4p.createExecution(context, execution("excess", 1, 1), NOW + 30_000));
        assertEquals(IntentExecutionStatus.ACTIVE, context.status());
        assertEquals(0, info(context).leftExecutionCount());
    }

    @Test
    void maximumSafePeriodDoesNotOverflowAndGeneratedIdsAreStoredInLedger() {
        long maxSafe = CanonicalJson.MAX_SAFE_INTEGER;
        PaymentConstraints constraints = new PaymentConstraints(null, maxSafe, "CNY",
                0L, maxSafe, null, 0L, Validation.MAX_PERIOD_SECONDS, 1L);
        IntentExecutionContext context = ok(A4p.activateIntent(authorized(constraints), VERIFIER, maxSafe));
        assertEquals(1, info(context).periodIndex());
        Execution execution = execution(null, maxSafe, 1);
        ok(A4p.createExecution(context, execution, 0));
        assertNull(execution.id());
        assertFalse(context.executions().get(0).executionId().isEmpty());
        assertEquals(context.executions().get(0).executionId(), context.executions().get(0).execution().id());
        assertEquals(0, info(context).leftAmount());
        fails(A4P_AMOUNT_EXCEEDED, A4p.createExecution(context, execution("extra", 1, 1), maxSafe));
        PaymentConstraints tooLong = new PaymentConstraints(null, maxSafe, "CNY",
                0L, maxSafe, null, 0L, Validation.MAX_PERIOD_SECONDS + 1, 1L);
        fails(A4P_INVALID_PARAMETER, A4p.createIntent(request(tooLong), SIGNER, NOW));
    }

    @Test
    void concurrentReservationsCannotOvercommit() throws Exception {
        IntentExecutionContext context = context();
        var executor = Executors.newFixedThreadPool(8);
        try {
            List<Callable<A4pResult<IntentExecutionContext>>> tasks = new ArrayList<>();
            for (int index = 0; index < 20; index++) {
                String id = "concurrent-" + index;
                tasks.add(() -> A4p.createExecution(context, execution(id, 100, 1), NOW));
            }
            int accepted = 0;
            for (var future : executor.invokeAll(tasks)) {
                A4pResult<IntentExecutionContext> result = future.get();
                if (result.success()) {
                    accepted++;
                } else {
                    assertEquals(A4P_PERIOD_EXECUTION_COUNT_EXCEEDED.code(), result.code());
                }
            }
            assertEquals(2, accepted);
            assertEquals(800, info(context).leftAmount());
        } finally {
            executor.shutdownNow();
        }
    }

    @Test
    void blankTextAndProductIdentitiesAreRejected() {
        PaymentConstraints blankCurrency = new PaymentConstraints(null, 100L, " ", 0L, 100L, null, null, null, null);
        fails(A4P_INVALID_PARAMETER, A4p.createIntent(request(blankCurrency), SIGNER, NOW));
        fails(A4P_INVALID_PARAMETER, A4p.createIntent(new CreateIntentRequest("conversation", "\u3000 \n",
                request(constraints()).structuredIntent(), null), SIGNER, NOW));
        fails(A4P_INVALID_PARAMETER, A4p.createIntent(
                requestFor(new Product(new SellerInfo(" "), new SpuSkuInfo("spu", "sku"))), SIGNER, NOW));
        fails(A4P_INVALID_PARAMETER, A4p.createIntent(
                requestFor(new Product(new SellerInfo("seller"), new SpuSkuInfo(null, null))), SIGNER, NOW));
        fails(A4P_INVALID_PARAMETER, A4p.createIntent(
                requestFor(new Product(new SellerInfo("seller"), new SpuSkuInfo("\t", "sku"))), SIGNER, NOW));
        Product skuOnly = new Product(new SellerInfo("seller"), new SpuSkuInfo(null, "sku"));
        IntentExecutionContext context = activate(requestFor(skuOnly));
        fails(A4P_PRODUCT_NOT_AUTHORIZED, A4p.createExecution(context, execution("spu", 1, 1), NOW));
        fails(A4P_INVALID_PARAMETER, A4p.createExecution(context, execution(" ", 1, 1), NOW));
        fails(A4P_INVALID_PARAMETER, A4p.createExecution(context, execution("", 1, 1), NOW));
        fails(A4P_INVALID_PARAMETER, A4p.createExecution(context, withOrderId(execution("blank-order", 1, 1), " "), NOW));
        ok(A4p.createExecution(context, cart("sku-only", 1, new CartItem(skuOnly, 1L)), NOW));
        assertEquals(1, context.executions().size());
    }

    @Test
    void periodAnchorMustNotFollowValidFrom() {
        PaymentConstraints late = new PaymentConstraints(null, 100L, "CNY", 0L, 100L, null, 1L, 10L, 1L);
        fails(A4P_INVALID_PARAMETER, A4p.createIntent(request(late), SIGNER, NOW));
        PaymentConstraints same = new PaymentConstraints(null, 100L, "CNY", 1L, 100L, null, 1L, 10L, 1L);
        ok(A4p.createIntent(request(same), SIGNER, NOW));
    }

    @Test
    void unknownAlgorithmsAreRejectedStructurally() {
        IntentL1 intent = ok(A4p.createIntent(request(constraints()), SIGNER, NOW));
        IntentL1 relabeled = new IntentL1(intent.version(), intent.intentId(), intent.intentConversation(),
                intent.intentSummary(), intent.structuredIntent(), intent.timestamp(),
                new SignatureInfo("HS256", intent.sign().keyIdentifier(), intent.sign().sign()));
        fails(A4P_INVALID_PARAMETER, A4p.issueIntent(relabeled, VERIFIER, issueContext(), SIGNER, NOW));
        A4pIntentCodec codec = A4pIntentCodec.standard();
        fails(A4P_INVALID_PARAMETER, codec.deserializeIntentL1(codec.serializeIntentL1(relabeled)));
        Signer foreign = data -> new SignatureInfo("HS256", "key", "c2ln");
        fails(A4P_SIGNING_FAIL, A4p.createIntent(request(constraints()), foreign, NOW));
    }

    @Test
    void chainTimestampsMustBeMonotonic() {
        IntentL1 first = ok(A4p.createIntent(request(constraints()), SIGNER, NOW));
        fails(A4P_INVALID_PARAMETER, A4p.issueIntent(first, VERIFIER, issueContext(), SIGNER, NOW - 1));
        IntentL2 second = ok(A4p.issueIntent(first, VERIFIER, issueContext(), SIGNER, NOW + 5));
        IntentL2 backdated = new IntentL2(second.version(), first, second.userIdentity(), second.agentIdentity(),
                second.serviceProviderIdentity(), NOW - 1, second.sign());
        SigningEnv environment = new SigningEnv(Map.of(), Map.of());
        fails(A4P_INVALID_PARAMETER, A4p.signIntent(backdated, VERIFIER, environment, SIGNER, NOW + 5));
        fails(A4P_INVALID_PARAMETER, A4p.signIntent(second, VERIFIER, environment, SIGNER, NOW));
        IntentL3 third = ok(A4p.signIntent(second, VERIFIER, environment, SIGNER, NOW + 5));
        IntentL3 earlier = new IntentL3(third.version(), second, environment, NOW, third.sign());
        fails(A4P_INVALID_PARAMETER, A4p.activateIntent(earlier, VERIFIER, NOW + 5));
        fails(A4P_INVALID_PARAMETER, A4p.verifyIntentAuthenticity(earlier, VERIFIER));
        fails(A4P_INVALID_PARAMETER, A4p.activateIntent(third, VERIFIER, NOW + 4));
        assertEquals(IntentExecutionStatus.ACTIVE, ok(A4p.activateIntent(third, VERIFIER, NOW + 5)).status());
    }

    @Test
    void activationRejectsExpiredIntentsButAcceptsNotYetValidOnes() {
        IntentL3 intent = authorized(new PaymentConstraints(null, 1000L, "CNY", 20_000L, 100_000L,
                null, null, null, null));
        fails(A4P_TIMESTAMP_EXPIRED, A4p.activateIntent(intent, VERIFIER, 100_001));
        assertEquals(IntentExecutionStatus.ACTIVE, ok(A4p.activateIntent(intent, VERIFIER, 100_000)).status());
        assertEquals(IntentExecutionStatus.ACTIVE, ok(A4p.activateIntent(intent, VERIFIER, NOW)).status());
        // The user signature is checked first, so a forged expired intent reports the forgery.
        SignatureVerifier reject = (data, sign) -> new VerificationResult(false, A4P_VERIFY_FAIL.code(), null);
        fails(A4P_VERIFY_FAIL, A4p.activateIntent(intent, reject, 100_001));
    }

    @Test
    void orderIdIsIdempotencyKeyIncludingCanceledRecords() {
        IntentExecutionContext context = context();
        Execution first = execution("first", 100, 1);
        ok(A4p.createExecution(context, first, NOW));
        String orderId = orderId(first);
        fails(A4P_DUPLICATE_ID, A4p.createExecution(context, withOrderId(execution("again", 100, 1), orderId), NOW));
        ok(A4p.cancelExecution(context, "first", NOW));
        fails(A4P_DUPLICATE_ID, A4p.createExecution(context, withOrderId(execution("retry", 100, 1), orderId), NOW));
        assertEquals(1, context.executions().size());
        assertEquals(1000, info(context).leftAmount());
    }

    @Test
    void checkOrderReportsParameterAndExpiryBeforeLedgerConflicts() {
        IntentExecutionContext context = context();
        ok(A4p.createExecution(context, execution("first", 100, 1), NOW));
        Execution foreign = PaymentProfile.execution("first", "1.0",
                new Order("foreign", 200_000L, "USD", List.of(new CartItem(PRODUCT, 1L)), NOW));
        fails(A4P_INVALID_PARAMETER, A4p.createExecution(context, foreign, NOW));
        ok(A4p.revokeIntent(context));
        fails(A4P_INVALID_PARAMETER, A4p.createExecution(context, foreign, NOW));
        fails(A4P_INVALID_STATUS, A4p.createExecution(context, execution("first", 100, 1), NOW));
        IntentExecutionContext expiring = context();
        Execution reserved = execution("reserved", 100, 1);
        ok(A4p.createExecution(expiring, reserved, NOW));
        fails(A4P_TIMESTAMP_EXPIRED, A4p.createExecution(expiring,
                withOrderId(execution("reserved", 100, 1), orderId(reserved)), 100_001));
        assertEquals(IntentExecutionStatus.EXPIRED, expiring.status());
    }

    @Test
    void invalidNowIsRejectedBeforeAnythingElse() {
        fails(A4P_INVALID_PARAMETER, A4p.createIntent(request(constraints()), SIGNER, -1));
        fails(A4P_INVALID_PARAMETER, A4p.activateIntent(authorized(constraints()), VERIFIER,
                CanonicalJson.MAX_SAFE_INTEGER + 1));
        IntentExecutionContext context = context();
        fails(A4P_INVALID_PARAMETER, A4p.createExecution(context, execution("one", 100, 1), -1));
        ok(A4p.createExecution(context, execution("one", 100, 1), NOW));
        fails(A4P_INVALID_PARAMETER, A4p.completeExecution(context, "one", -1));
        assertEquals(ExecutionStatus.PENDING, context.executions().get(0).status());
    }

    @Test
    void resultModelSeparatesProtocolFailuresFromSdkDefects() {
        A4pResult<String> success = A4pResult.ok("data");
        assertTrue(success.success());
        assertEquals(0, success.code());
        A4pResult<String> failure = A4pResult.failure(A4P_AMOUNT_EXCEEDED);
        assertFalse(failure.success());
        assertNull(failure.data());
        assertEquals(4002, failure.code());
        assertEquals("Amount exceeded", failure.message());
        assertEquals(A4P_INTERNAL_ERROR.code(), A4pResult.failure(A4P_SUCCESS).code());
        fails(A4P_DUPLICATE_ID, A4pResult.capture(() -> { throw new A4pException(A4P_DUPLICATE_ID); }));
        A4pException defect = assertThrows(A4pException.class,
                () -> A4pResult.capture(() -> { throw new IllegalStateException("bug"); }));
        assertEquals(A4P_INTERNAL_ERROR, defect.code());
        assertInstanceOf(IllegalStateException.class, defect.getCause());
    }
}
