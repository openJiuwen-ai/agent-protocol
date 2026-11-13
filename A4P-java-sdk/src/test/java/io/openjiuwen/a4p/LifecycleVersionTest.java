package io.openjiuwen.a4p;

import com.fasterxml.jackson.databind.node.ObjectNode;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.ValueSource;

import static io.openjiuwen.a4p.A4pErrorCode.A4P_UNSUPPORTED_VERSION;
import static io.openjiuwen.a4p.A4pErrorCode.A4P_INVALID_PARAMETER;
import static io.openjiuwen.a4p.Fixtures.*;
import static org.junit.jupiter.api.Assertions.*;

class LifecycleVersionTest {
    private static final String UNSUPPORTED_VERSION = "2.0";
    private final A4pIntentCodec codec = new A4pIntentCodec();

    /** Checks the version assigned by creation and the version required by issuance. */
    @Test
    void creationUsesSdkVersionAndIssuanceRejectsUnsupportedVersion() throws Exception {
        var intent = ok(A4p.createIntent(request(constraints()), SIGNER, NOW));
        assertEquals(A4p.PROTOCOL_VERSION, intent.version());
        var json = (ObjectNode) CanonicalJson.MAPPER.readTree(codec.serializeIntentL1(intent));
        json.put("version", UNSUPPORTED_VERSION);
        var unsupported = ok(codec.deserializeIntentL1(CanonicalJson.stringify(json)));

        fails(A4P_UNSUPPORTED_VERSION, A4p.issueIntent(unsupported,
                (data, signature) -> fail("Issuance must reject the version before verification"),
                issueContext(), data -> fail("Issuance must reject the version before signing"), NOW));
    }

    /**
     * Checks activation and authenticity verification for each incompatible layer and a uniformly incompatible chain.
     * @param layer intent layer to change, or 0 to change all three
     */
    @ParameterizedTest
    @ValueSource(ints = {1, 2, 3, 0})
    void rejectsUnsupportedIntentVersions(int layer) throws Exception {
        var json = (ObjectNode) CanonicalJson.MAPPER.readTree(codec.serializeIntentL3(authorized(constraints())));
        changeIntentVersion(json, layer);
        var unsupported = ok(codec.deserializeIntentL3(CanonicalJson.stringify(json)));
        SignatureVerifier verifier = (data, signature) -> fail("Version must be checked before verification");

        fails(A4P_UNSUPPORTED_VERSION, A4p.activateIntent(unsupported, verifier, NOW));
        fails(A4P_UNSUPPORTED_VERSION, A4p.verifyIntentAuthenticity(unsupported, verifier));
    }

    /**
     * Checks every Context lifecycle operation with a valid pending execution and incompatible versions.
     * @param layer intent layer to change, 4 for Context only, or 0 for Context and all intent layers
     */
    @ParameterizedTest
    @ValueSource(ints = {1, 2, 3, 4, 0})
    void rejectsUnsupportedContextVersionsWithoutMutatingState(int layer) throws Exception {
        var context = context();
        assertEquals(A4p.PROTOCOL_VERSION, context.version());
        ok(A4p.createExecution(context, execution("pending", 100, 1), NOW));
        var json = (ObjectNode) CanonicalJson.MAPPER.readTree(codec.serializeIntentExecutionContext(context));
        if (layer == 4 || layer == 0) {
            json.put("version", UNSUPPORTED_VERSION);
        }
        changeIntentVersion((ObjectNode) json.get("authorizedIntent"), layer);
        String serialized = CanonicalJson.stringify(json);
        var restored = ok(codec.deserializeIntentExecutionContext(serialized));

        fails(A4P_UNSUPPORTED_VERSION, A4p.createExecution(restored, execution("new", 100, 1), NOW));
        fails(A4P_UNSUPPORTED_VERSION, A4p.completeExecution(restored, "pending", NOW));
        fails(A4P_UNSUPPORTED_VERSION, A4p.cancelExecution(restored, "pending", NOW));
        fails(A4P_UNSUPPORTED_VERSION, A4p.revokeIntent(restored, NOW));
        fails(A4P_UNSUPPORTED_VERSION, A4p.revokeIntent(restored, -1));
        assertEquals(serialized, codec.serializeIntentExecutionContext(restored));
    }

    /** Checks that restored contexts with invalid signature metadata cannot mutate the ledger or status. */
    @Test
    void rejectsMalformedAuthorizedIntentWithoutMutatingContext() throws Exception {
        var context = context();
        ok(A4p.createExecution(context, execution("pending", 100, 1), NOW));
        var json = (ObjectNode) CanonicalJson.MAPPER.readTree(codec.serializeIntentExecutionContext(context));
        ((ObjectNode) json.at("/authorizedIntent/sign")).put("keyIdentifier", "");
        String serialized = CanonicalJson.stringify(json);
        var restored = ok(codec.deserializeIntentExecutionContext(serialized));

        fails(A4P_INVALID_PARAMETER, A4p.createExecution(restored, execution("new", 100, 1), NOW));
        fails(A4P_INVALID_PARAMETER, A4p.completeExecution(restored, "pending", NOW));
        fails(A4P_INVALID_PARAMETER, A4p.cancelExecution(restored, "pending", NOW));
        fails(A4P_INVALID_PARAMETER, A4p.revokeIntent(restored, NOW));
        assertEquals(serialized, codec.serializeIntentExecutionContext(restored));
    }

    /**
     * Sets unsupported versions at the selected intent layers.
     * @param l3 JSON for the third-layer intent
     * @param layer intent layer to change, 0 for all layers, or 4 to leave the chain as supplied
     */
    private static void changeIntentVersion(ObjectNode l3, int layer) {
        ObjectNode l2 = (ObjectNode) l3.get("intentL2");
        ObjectNode l1 = (ObjectNode) l2.get("intentL1");
        if (layer == 0 || layer == 1) {
            l1.put("version", UNSUPPORTED_VERSION);
        }
        if (layer == 0 || layer == 2) {
            l2.put("version", UNSUPPORTED_VERSION);
        }
        if (layer == 0 || layer == 3) {
            l3.put("version", UNSUPPORTED_VERSION);
        }
    }
}
