package io.openjiuwen.a4p;

import com.fasterxml.jackson.databind.JsonNode;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Map;

public final class Models {
    private Models() {}

    // Every enum is serialized by name, e.g. "AI_PAY".
    public enum IntentActionType { AI_PAY }

    public enum AlgorithmType { RSA3072 }

    public enum IntentExecutionStatus { ACTIVE, CONSUMED, REVOKED, EXPIRED }

    public enum ExecutionStatus { PENDING, COMPLETED, CANCELED }

    // AI_PAY payload types; PaymentProfile maps them to and from the JsonNode fields below.
    public record SellerInfo(String id) {}
    public record SpuSkuInfo(String spuId, String skuId) {}
    public record Product(SellerInfo seller, SpuSkuInfo spusku) {}
    public record CartItem(Product product, Long quantity) {}

    public record ItemWhiteList(List<Product> whiteList, Long quantity) {
        public ItemWhiteList { whiteList = snapshot(whiteList); }
    }

    public record ShoppingItems(List<ItemWhiteList> itemsWhiteList) {
        public ShoppingItems { itemsWhiteList = snapshot(itemsWhiteList); }
    }

    public record PaymentConstraints(Long singleOrderMaxAmount, Long totalMaxAmount, String currency,
                                     Long validFrom, Long validUntil, Long maxExecuteCount,
                                     Long anchorTime, Long periodSeconds, Long maxExecutionCountPerPeriod) {}

    public record Order(String orderId, Long amount, String currency, List<CartItem> cartItems, Long timestamp) {
        public Order { cartItems = snapshot(cartItems); }
    }

    // Non-applicable figures are null and still serialized, so every field is always declared.
    public record PaymentExecutionInfo(Long leftExecutionCount, long leftAmount, Long periodIndex,
                                       Long leftPeriodExecutionCount) {}

    // Typed union: the action selects the schema of resources and constraints.
    public record StructuredIntent(IntentActionType action, JsonNode resources, JsonNode constraints) {
        public StructuredIntent {
            resources = CanonicalJson.freezeNode(resources);
            constraints = CanonicalJson.freezeNode(constraints);
        }

        @Override
        public JsonNode resources() { return CanonicalJson.freezeNode(resources); }

        @Override
        public JsonNode constraints() { return CanonicalJson.freezeNode(constraints); }
    }

    public record UserIdentity(String identityProvider, String id, String additionalInfo) {}
    public record AgentIdentity(String identityProvider, String id, String additionalInfo) {}
    public record ServiceProviderIdentity(String id, String additionalInfo) {}

    public record SigningEnv(Map<String, Object> device, Map<String, Object> app) {
        public SigningEnv {
            device = CanonicalJson.freezeMap(device);
            app = CanonicalJson.freezeMap(app);
        }
    }

    public record SignatureInfo(String alg, String keyIdentifier, String sign) {
        public SignatureInfo(AlgorithmType alg, String keyIdentifier, String sign) {
            this(alg == null ? null : alg.name(), keyIdentifier, sign);
        }
    }

    public record CreateIntentRequest(String intentConversation, String intentSummary,
                                      StructuredIntent structuredIntent, String intentId) {}
    public record IntentIssueContext(UserIdentity userIdentity, AgentIdentity agentIdentity,
                                     ServiceProviderIdentity serviceProviderIdentity) {}
    public record VerificationResult(boolean valid, long code, String message) {}
    public record IntentL1(String version, String intentId, String intentConversation, String intentSummary,
                           StructuredIntent structuredIntent, Long timestamp, SignatureInfo sign) {}
    public record IntentL2(String version, IntentL1 intentL1, UserIdentity userIdentity,
                           AgentIdentity agentIdentity, ServiceProviderIdentity serviceProviderIdentity,
                           Long timestamp, SignatureInfo sign) {}
    public record IntentL3(String version, IntentL2 intentL2, SigningEnv signingEnv,
                           Long timestamp, SignatureInfo sign) {}

    public record Execution(IntentActionType action, String id, String version, JsonNode executionContent) {
        public Execution { executionContent = CanonicalJson.freezeNode(executionContent); }

        @Override
        public JsonNode executionContent() { return CanonicalJson.freezeNode(executionContent); }
    }

    public record ExecutionRecord(String executionId, ExecutionStatus status, Long periodIndex,
                                  long reservedAt, Long completedAt, Long canceledAt, Execution execution) {}

    private static <Value> List<Value> snapshot(List<Value> values) {
        return values == null ? null : Collections.unmodifiableList(new ArrayList<>(values));
    }
}
