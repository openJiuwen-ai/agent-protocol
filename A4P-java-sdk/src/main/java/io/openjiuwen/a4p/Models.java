package io.openjiuwen.a4p;

import com.fasterxml.jackson.databind.JsonNode;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Map;

/**
 * Contains the protocol records and enums used by the A4P Java SDK.
 * Record constructors describe data, but do not replace lifecycle validation or authorization.
 * Use A4p to create layered intents and PaymentProfile to construct AI_PAY payloads.
 * Amounts use application-defined minor currency units; protocol numbers are safe integers.
 * Optional components use null and are omitted from canonical record JSON.
 * Collections are copied, maps are frozen, and JSON payload accessors return defensive copies.
 * @see PaymentProfile
 */
public final class Models {
    /**
     * Prevents instantiation of this utility class.
     */
    private Models() {}

    // Every enum is serialized by name, e.g. "AI_PAY".
    /**
     * Defines supported action discriminators; enum names are serialized as strings.
     */
    public enum IntentActionType {
        /** Authorizes AI-initiated payment using the payment resource and constraint schemas. */
        AI_PAY
    }

    /**
     * Defines supported signature algorithm identifiers.
     */
    public enum AlgorithmType {
        /** Identifies the RSA-3072 signing algorithm supported by this protocol version. */
        RSA3072
    }

    /**
     * Defines context states: ACTIVE accepts reservations; CONSUMED has exhausted global quota with no pending records.
     * REVOKED and EXPIRED reject new reservations but allow finalization of existing pending records.
     */
    public enum IntentExecutionStatus {
        /** Accepts new reservations within the validity window and remaining quotas. */
        ACTIVE,
        /** Has exhausted a global quota and has no pending executions. */
        CONSUMED,
        /** Rejects new reservations while allowing existing pending records to be finalized. */
        REVOKED,
        /** Rejects new reservations after expiry while allowing pending records to be finalized. */
        EXPIRED
    }

    /**
     * Defines execution states: PENDING occupies quota, COMPLETED retains usage, and CANCELED releases usage.
     */
    public enum ExecutionStatus {
        /** Reserves amount and execution-count quotas while awaiting an external outcome. */
        PENDING,
        /** Confirms execution and retains occupied quotas. */
        COMPLETED,
        /** Releases occupied quotas while retaining the record and its identifiers. */
        CANCELED
    }

    // AI_PAY payload types; PaymentProfile maps them to and from the JsonNode fields below.
    /**
     * Identifies the authorized seller.
     * @param id nonblank identity identifier
     */
    public record SellerInfo(String id) {}
    /**
     * Identifies a product by SPU, SKU or both; at least one identifier must be nonblank.
     * @param spuId SPU identifier; optional when skuId is present
     * @param skuId SKU identifier; optional when spuId is present
     */
    public record SpuSkuInfo(String spuId, String skuId) {}
    /**
     * Combines seller and product identifiers for exact whitelist matching.
     * @param seller authorized seller identity
     * @param spusku SPU and SKU product identity
     */
    public record Product(SellerInfo seller, SpuSkuInfo spusku) {}
    /**
     * Specifies one selected product and its positive per-order quantity.
     * @param product seller and SPU/SKU product identity
     * @param quantity positive quantity required for each execution
     */
    public record CartItem(Product product, Long quantity) {}

    // Per-execution template quantity, not a lifetime quota. Products must be unique across all groups.
    /**
     * Defines alternative products (OR) and the exact quantity required for this group in each execution.
     * Groups form an AND template, and products must be disjoint across groups; quantities are not lifetime quotas.
     * @param whiteList alternative authorized products in this group
     * @param quantity positive quantity required for each execution
     */
    public record ItemWhiteList(List<Product> whiteList, Long quantity) {
        /**
         * Constructs a defensive snapshot of the supplied record components.
         * @param whiteList alternative authorized products in this group
         * @param quantity positive quantity required for each execution
         */
        public ItemWhiteList { whiteList = snapshot(whiteList); }
    }

    /**
     * Defines all required payment whitelist groups (AND); each execution selects one product from every group.
     * @param itemsWhiteList all required, mutually disjoint whitelist groups
     */
    public record ShoppingItems(List<ItemWhiteList> itemsWhiteList) {
        /**
         * Constructs a defensive snapshot of the supplied record components.
         * @param itemsWhiteList all required, mutually disjoint whitelist groups
         */
        public ShoppingItems { itemsWhiteList = snapshot(itemsWhiteList); }
    }

    /**
     * Defines AI_PAY amount, currency, validity and execution-count limits.
     * Amounts are positive integers in application-defined minor currency units.
     * Periodic settings must be supplied together or all omitted; anchorTime must be at or before validFrom,
     * and periodSeconds must be positive and no greater than 9007199254740.
     * @param singleOrderMaxAmount optional positive amount cap for each order
     * @param totalMaxAmount positive global authorized amount cap
     * @param currency nonblank currency shared by constraints and orders
     * @param validFrom inclusive authorization start in Unix epoch milliseconds
     * @param validUntil inclusive authorization end in Unix epoch milliseconds
     * @param maxExecutionCount optional positive global count cap
     * @param anchorTime optional period anchor in epoch milliseconds, no later than validFrom
     * @param periodSeconds optional positive period length in seconds
     * @param maxExecutionCountPerPeriod optional positive count cap for each period
     */
    public record PaymentConstraints(Long singleOrderMaxAmount, Long totalMaxAmount, String currency,
                                     Long validFrom, Long validUntil, Long maxExecutionCount,
                                     Long anchorTime, Long periodSeconds, Long maxExecutionCountPerPeriod) {}

    /**
     * Describes a payment order and its cart.
     * The trusted SDK evaluation clock, rather than this order timestamp, controls validity and quota periods.
     * @param orderId nonblank payment idempotency key, unique within the context even after cancellation
     * @param amount positive payment amount in application-defined minor currency units
     * @param currency nonblank currency shared by constraints and orders
     * @param cartItems one selected product per whitelist group at its declared quantity
     * @param timestamp order timestamp in epoch milliseconds; not the trusted quota clock
     */
    public record Order(String orderId, Long amount, String currency, List<CartItem> cartItems, Long timestamp) {
        /**
         * Constructs a defensive snapshot of the supplied record components.
         * @param orderId nonblank payment idempotency key, unique within the context even after cancellation
         * @param amount positive payment amount in application-defined minor currency units
         * @param currency nonblank currency shared by constraints and orders
         * @param cartItems one selected product per whitelist group at its declared quantity
         * @param timestamp order timestamp in epoch milliseconds; not the trusted quota clock
         */
        public Order { cartItems = snapshot(cartItems); }
    }

    // Non-applicable figures are null in Java and omitted from context JSON.
    /**
     * Captures remaining quotas after subtracting PENDING and COMPLETED usage.
     * Optional figures are null when unconfigured. Reads do not refresh this snapshot or its period.
     * @param leftExecutionCount remaining global count, or null when unconfigured
     * @param leftAmount remaining global amount after pending and completed reservations
     * @param periodIndex snapshot period, or null without periodic limits
     * @param leftPeriodExecutionCount remaining count for the snapshot period, or null when unconfigured
     */
    public record PaymentExecutionInfo(Long leftExecutionCount, long leftAmount, Long periodIndex,
                                       Long leftPeriodExecutionCount) {}

    // Typed union: the action selects the schema of resources and constraints.
    /**
     * Selects resources and constraints schemas by action; AI_PAY uses the payment profile.
     * JSON inputs and returned payloads are copied defensively.
     * @param action protocol action selecting the payload schema
     * @param resources authorized AI_PAY shopping whitelist payload
     * @param constraints authorized AI_PAY payment limits
     */
    public record StructuredIntent(IntentActionType action, JsonNode resources, JsonNode constraints) {
        /**
         * Constructs a defensive snapshot of the supplied record components.
         * @param action protocol action selecting the payload schema
         * @param resources authorized AI_PAY shopping whitelist payload
         * @param constraints authorized AI_PAY payment limits
         */
        public StructuredIntent {
            resources = CanonicalJson.freezeNode(resources);
            constraints = CanonicalJson.freezeNode(constraints);
        }

        /**
         * Returns a defensive copy of this record payload.
         * @return defensive JSON copy of the resources payload
         */
        @Override
        public JsonNode resources() { return CanonicalJson.freezeNode(resources); }

        /**
         * Returns a defensive copy of this record payload.
         * @return defensive JSON copy of the constraints payload
         */
        @Override
        public JsonNode constraints() { return CanonicalJson.freezeNode(constraints); }
    }

    /**
     * Identifies the user through a trusted identity provider.
     * @param identityProvider trusted identity provider identifier
     * @param id nonblank identity identifier
     * @param additionalInfo optional additional identity information
     */
    public record UserIdentity(String identityProvider, String id, String additionalInfo) {}
    /**
     * Identifies the agent through a trusted identity provider.
     * @param identityProvider trusted identity provider identifier
     * @param id nonblank identity identifier
     * @param additionalInfo optional additional identity information
     */
    public record AgentIdentity(String identityProvider, String id, String additionalInfo) {}
    /**
     * Identifies the service provider bound by the intent center.
     * @param id nonblank identity identifier
     * @param additionalInfo optional additional identity information
     */
    public record ServiceProviderIdentity(String id, String additionalInfo) {}

    /**
     * Captures user signing device and application information as immutable normalized maps.
     * @param device signing device information
     * @param app signing application information
     */
    public record SigningEnv(Map<String, Object> device, Map<String, Object> app) {
        /**
         * Constructs a defensive snapshot of the supplied record components.
         * @param device signing device information
         * @param app signing application information
         */
        public SigningEnv {
            device = CanonicalJson.freezeMap(device);
            app = CanonicalJson.freezeMap(app);
        }
    }

    /**
     * Carries an algorithm name, key identifier and unpadded Base64URL signature.
     * The outer signature metadata is not signed by its own layer; verifiers must use trusted key bindings.
     * @param alg supported signature algorithm
     * @param keyIdentifier identifier used to resolve a trusted signing key
     * @param sign unpadded Base64URL signature bytes
     */
    public record SignatureInfo(String alg, String keyIdentifier, String sign) {
        /**
         * Constructs signature metadata using the protocol enum name.
         * @param alg supported signature algorithm
         * @param keyIdentifier identifier used to resolve a trusted signing key
         * @param sign unpadded Base64URL signature bytes
         */
        public SignatureInfo(AlgorithmType alg, String keyIdentifier, String sign) {
            this(alg == null ? null : alg.name(), keyIdentifier, sign);
        }
    }

    /**
     * Supplies the conversation, display summary and structured authorization for creating L1.
     * The agent enforces conversation length and sensitive-data filtering before submission.
     * @param intentConversation nonblank original conversation; length and sensitive-data filtering are the agent responsibility
     * @param intentSummary nonblank summary displayed for authorization
     * @param structuredIntent action-selected resources and constraints
     * @param intentId optional globally unique identifier; null requests UUID generation
     */
    public record CreateIntentRequest(String intentConversation, String intentSummary,
                                      StructuredIntent structuredIntent, String intentId) {}
    /**
     * Supplies trusted identities for issuing L2; this is not an activated execution context.
     * @param userIdentity trusted user identity
     * @param agentIdentity trusted agent identity
     * @param serviceProviderIdentity trusted service provider identity
     */
    public record IntentIssueContext(UserIdentity userIdentity, AgentIdentity agentIdentity,
                                     ServiceProviderIdentity serviceProviderIdentity) {}
    /**
     * Represents a verification verdict; valid must be true exactly when code is zero.
     * @param valid true exactly when the verification code is zero
     * @param code protocol result code; zero means success
     * @param message diagnostic message
     */
    public record VerificationResult(boolean valid, long code, String message) {}
    /**
     * Represents the agent-created first authorization layer.
     * Obtain authorized lifecycle objects through A4p; constructing a record does not authorize execution.
     * @param version A4P Core protocol version
     * @param intentId globally unique intent identifier
     * @param intentConversation nonblank original conversation; length and sensitive-data filtering are the agent responsibility
     * @param intentSummary nonblank summary displayed for authorization
     * @param structuredIntent action-selected resources and constraints
     * @param timestamp Unix epoch milliseconds
     * @param sign signature metadata for this layer
     */
    public record IntentL1(String version, String intentId, String intentConversation, String intentSummary,
                           StructuredIntent structuredIntent, Long timestamp, SignatureInfo sign) {}
    /**
     * Represents the intent-center-issued second layer, embedding the complete signed L1 and trusted identities.
     * @param version A4P Core protocol version
     * @param intentL1 SDK-created first-layer intent
     * @param userIdentity trusted user identity
     * @param agentIdentity trusted agent identity
     * @param serviceProviderIdentity trusted service provider identity
     * @param timestamp Unix epoch milliseconds
     * @param sign signature metadata for this layer
     */
    public record IntentL2(String version, IntentL1 intentL1, UserIdentity userIdentity,
                           AgentIdentity agentIdentity, ServiceProviderIdentity serviceProviderIdentity,
                           Long timestamp, SignatureInfo sign) {}
    /**
     * Represents the user-signed third layer, embedding the complete signed L2 and signing environment.
     * @param version A4P Core protocol version
     * @param intentL2 SDK-issued second-layer intent
     * @param signingEnv user-device signing environment
     * @param timestamp Unix epoch milliseconds
     * @param sign signature metadata for this layer
     */
    public record IntentL3(String version, IntentL2 intentL2, SigningEnv signingEnv,
                           Long timestamp, SignatureInfo sign) {}

    /**
     * Requests an action-specific execution; AI_PAY executionContent contains an Order.
     * A missing ID is generated during reservation, and JSON payloads are copied defensively.
     * @param action protocol action selecting the payload schema
     * @param id optional execution identifier; null requests generation during reservation
     * @param executionContent action-specific JSON payload; AI_PAY contains an Order
     */
    public record Execution(IntentActionType action, String id, JsonNode executionContent) {
        /**
         * Constructs a defensive snapshot of the supplied record components.
         * @param action protocol action selecting the payload schema
         * @param id optional execution identifier; null requests generation during reservation
         * @param executionContent action-specific JSON payload; AI_PAY contains an Order
         */
        public Execution { executionContent = CanonicalJson.freezeNode(executionContent); }

        /**
         * Returns a defensive copy of this record payload.
         * @return defensive JSON copy of the execution payload
         */
        @Override
        public JsonNode executionContent() { return CanonicalJson.freezeNode(executionContent); }
    }

    /**
     * Records a reservation and its terminal outcome.
     * The reservation period remains fixed across completion or cancellation in later periods.
     * Canceled records retain their identifiers but release quotas; JSON payloads are copied defensively.
     * @param executionId execution identifier, retained even after cancellation
     * @param status execution record state
     * @param action protocol action selecting the payload schema
     * @param executionContent action-specific JSON payload; AI_PAY contains an Order
     * @param periodIndex original reservation period, or null without periodic limits
     * @param reservedAt reservation instant in Unix epoch milliseconds
     * @param completedAt completion instant, present only for COMPLETED and not before reservedAt
     * @param canceledAt cancellation instant, present only for CANCELED and not before reservedAt
     */
    public record ExecutionRecord(String executionId, ExecutionStatus status, IntentActionType action,
                                  JsonNode executionContent, Long periodIndex, long reservedAt,
                                  Long completedAt, Long canceledAt) {
        /**
         * Constructs a defensive snapshot of the supplied record components.
         * @param executionId execution identifier, retained even after cancellation
         * @param status execution record state
         * @param action protocol action selecting the payload schema
         * @param executionContent action-specific JSON payload; AI_PAY contains an Order
         * @param periodIndex original reservation period, or null without periodic limits
         * @param reservedAt reservation instant in Unix epoch milliseconds
         * @param completedAt completion instant, present only for COMPLETED and not before reservedAt
         * @param canceledAt cancellation instant, present only for CANCELED and not before reservedAt
         */
        public ExecutionRecord { executionContent = CanonicalJson.freezeNode(executionContent); }

        /**
         * Returns a defensive copy of this record payload.
         * @return defensive JSON copy of the execution payload
         */
        @Override
        public JsonNode executionContent() { return CanonicalJson.freezeNode(executionContent); }
    }

    /**
     * Copies a nullable list into an unmodifiable snapshot.
     * @param <Value> decoded or copied value type
     * @param values list to snapshot or validate
     * @return unmodifiable list copy, or null for null input
     */
    private static <Value> List<Value> snapshot(List<Value> values) {
        return values == null ? null : Collections.unmodifiableList(new ArrayList<>(values));
    }
}
