package io.openjiuwen.a4p;

import io.openjiuwen.a4p.Models.*;
import java.security.MessageDigest;
import java.util.List;
import java.util.Map;
import java.util.concurrent.atomic.AtomicLong;
import org.junit.jupiter.api.function.Executable;
import static org.junit.jupiter.api.Assertions.*;

final class Fixtures {
    static final long NOW = 10_000;
    static final Product PRODUCT = new Product(new SellerInfo("seller"), new SpuSkuInfo("spu", "sku"));
    // Digest test double; it carries the RSA3072 label only because unknown algorithms are rejected structurally.
    static final Signer SIGNER = data -> new SignatureInfo(AlgorithmType.RSA3072, "key",
            CanonicalJson.base64UrlEncode(MessageDigest.getInstance("SHA-256").digest(data)));
    static final SignatureVerifier VERIFIER = (data, signature) -> {
        boolean valid = SIGNER.sign(data).equals(signature);
        return new VerificationResult(valid, valid ? 0 : A4pErrorCode.A4P_VERIFY_FAIL.code(), null);
    };
    // orderId is the idempotency key, so every fixture order needs a fresh one.
    private static final AtomicLong ORDERS = new AtomicLong();

    private Fixtures() {}

    static <Value> Value ok(A4pResult<Value> result) {
        assertTrue(result.success(), () -> "expected success, got " + result);
        assertEquals(A4pErrorCode.A4P_SUCCESS.code(), result.code());
        assertNotNull(result.data());
        return result.data();
    }

    static void fails(A4pErrorCode code, A4pResult<?> result) {
        assertFalse(result.success(), () -> "expected " + code + ", got success");
        assertNull(result.data());
        assertEquals(code.code(), result.code());
        assertEquals(code.message(), result.message());
    }

    static void thrown(A4pErrorCode code, Executable action) {
        assertEquals(code, assertThrows(A4pException.class, action).code());
    }

    static PaymentConstraints constraints() {
        return new PaymentConstraints(500L, 1000L, "CNY", 0L, 100_000L, 3L, 0L, 10L, 2L);
    }

    static StructuredIntent structured(List<ItemWhiteList> groups, PaymentConstraints constraints) {
        return PaymentProfile.structuredIntent(new ShoppingItems(groups), constraints);
    }

    static CreateIntentRequest request(PaymentConstraints constraints) {
        return new CreateIntentRequest("conversation", "summary",
                structured(List.of(new ItemWhiteList(List.of(PRODUCT), 5L)), constraints), "intent");
    }

    static CreateIntentRequest requestFor(Product product) {
        return new CreateIntentRequest("conversation", "summary",
                structured(List.of(new ItemWhiteList(List.of(product), 5L)), constraints()), null);
    }

    static IntentIssueContext issueContext() {
        return new IntentIssueContext(new UserIdentity("idp", "user", null),
                new AgentIdentity("idp", "agent", null), new ServiceProviderIdentity("service", null));
    }

    static IntentL3 authorized(CreateIntentRequest request) {
        IntentL1 first = ok(A4p.createIntent(request, SIGNER, NOW));
        IntentL2 second = ok(A4p.issueIntent(first, VERIFIER, issueContext(), SIGNER, NOW));
        return ok(A4p.signIntent(second, VERIFIER, new SigningEnv(Map.of("device", "test"), Map.of()), SIGNER, NOW));
    }

    static IntentL3 authorized(PaymentConstraints constraints) {
        return authorized(request(constraints));
    }

    static IntentExecutionContext activate(CreateIntentRequest request) {
        return ok(A4p.activateIntent(authorized(request), VERIFIER, NOW));
    }

    static IntentExecutionContext context() {
        return ok(A4p.activateIntent(authorized(constraints()), VERIFIER, NOW));
    }

    static PaymentExecutionInfo info(IntentExecutionContext context) {
        return PaymentProfile.executionInfo(context);
    }

    static Execution execution(String id, long amount, long quantity) {
        return cart(id, amount, new CartItem(PRODUCT, quantity));
    }

    static Execution cart(String id, long amount, CartItem... items) {
        return PaymentProfile.execution(id, "1.0",
                new Order("order-" + ORDERS.incrementAndGet(), amount, "CNY", List.of(items), 0L));
    }

    static Execution withOrderId(Execution execution, String orderId) {
        Order order = PaymentProfile.order(execution);
        return PaymentProfile.execution(execution.id(), execution.version(),
                new Order(orderId, order.amount(), order.currency(), order.cartItems(), order.timestamp()));
    }

    static String orderId(Execution execution) {
        return PaymentProfile.order(execution).orderId();
    }
}
