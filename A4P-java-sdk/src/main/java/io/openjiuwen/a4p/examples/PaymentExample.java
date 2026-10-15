package io.openjiuwen.a4p.examples;

import io.openjiuwen.a4p.*;
import io.openjiuwen.a4p.Models.*;
import java.security.KeyPairGenerator;
import java.util.List;
import java.util.Map;

public final class PaymentExample {
    private PaymentExample() {}

    public static void main(String[] args) throws Exception {
        KeyPairGenerator generator = KeyPairGenerator.getInstance("RSA");
        generator.initialize(3072);
        var agentKey = generator.generateKeyPair();
        var serverKey = generator.generateKeyPair();
        var userKey = generator.generateKeyPair();
        SignatureVerifier agentVerifier = Rsa3072.verifier(Map.of("agent", agentKey.getPublic()));
        SignatureVerifier serverVerifier = Rsa3072.verifier(Map.of("server", serverKey.getPublic()));
        SignatureVerifier userVerifier = Rsa3072.verifier(Map.of("user", userKey.getPublic()));
        long now = System.currentTimeMillis();
        Product product = new Product(new SellerInfo("shop"), new SpuSkuInfo("book", "paperback"));
        PaymentConstraints constraints = new PaymentConstraints(5000L, 10000L, "CNY", now,
                now + 86_400_000L, 2L, null, null, null);
        CreateIntentRequest request = new CreateIntentRequest("purchase", "Buy two books",
                PaymentProfile.structuredIntent(new ShoppingItems(List.of(new ItemWhiteList(List.of(product), 2L))),
                        constraints), null);
        IntentL1 proposal = unwrap(A4p.createIntent(request, Rsa3072.signer("agent", agentKey.getPrivate()), now));
        IntentIssueContext identities = new IntentIssueContext(new UserIdentity("example", "user-1", null),
                new AgentIdentity("example", "agent-1", null), new ServiceProviderIdentity("shop", null));
        IntentL2 issued = unwrap(A4p.issueIntent(proposal, agentVerifier, identities,
                Rsa3072.signer("server", serverKey.getPrivate()), now));
        IntentL3 authorized = unwrap(A4p.signIntent(issued, serverVerifier,
                new SigningEnv(Map.of("type", "desktop"), Map.of("name", "payment-example")),
                Rsa3072.signer("user", userKey.getPrivate()), now));
        IntentExecutionContext context = unwrap(A4p.activateIntent(authorized, userVerifier, now));
        System.out.println("Issued by intent center: "
                + unwrap(A4p.verifyIntentAuthenticity(authorized, serverVerifier)).valid());
        Execution execution = PaymentProfile.execution("execution-1", "1.0",
                new Order("order-1", 3000L, "CNY", List.of(new CartItem(product, 1L)), now));
        unwrap(A4p.createExecution(context, execution, now));
        unwrap(A4p.completeExecution(context, execution.id(), now));
        System.out.println("Intent: " + context.intentId() + " (" + context.status() + ")");
        System.out.println("Remaining amount: " + PaymentProfile.executionInfo(context).leftAmount());
        System.out.println(A4pIntentCodec.standard().serializeIntentL3(authorized));
    }

    private static <Value> Value unwrap(A4pResult<Value> result) {
        if (!result.success()) {
            throw new IllegalStateException(result.code() + " " + result.message());
        }
        return result.data();
    }
}
