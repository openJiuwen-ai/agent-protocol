package io.openjiuwen.a4p;

import io.openjiuwen.a4p.Models.*;
import static io.openjiuwen.a4p.Fixtures.*;

/** Run with the fully qualified name of a user A4pIntentCodec implementation. */
public final class CodecConformanceCheck {
    private CodecConformanceCheck() {}

    public static void main(String[] args) throws Exception {
        if (args.length != 1) {
            throw new IllegalArgumentException("Expected one A4pIntentCodec implementation class name");
        }
        Object instance = Class.forName(args[0]).getDeclaredConstructor().newInstance();
        if (!(instance instanceof A4pIntentCodec candidate)) {
            throw new IllegalArgumentException("Class must implement A4pIntentCodec");
        }
        A4pIntentCodec reference = new A4pIntentCodec();
        IntentL1 l1 = ok(A4p.createIntent(request(constraints()), SIGNER, NOW));
        IntentL2 l2 = ok(A4p.issueIntent(l1, VERIFIER, issueContext(), SIGNER, NOW));
        IntentL3 l3 = ok(A4p.signIntent(l2, VERIFIER, new SigningEnv(java.util.Map.of(),
                java.util.Map.of()), SIGNER, NOW));
        IntentExecutionContext context = ok(A4p.activateIntent(l3, VERIFIER, NOW));
        ok(A4p.createExecution(context, execution("example", 100, 1), NOW));
        compare("L1", reference.serializeIntentL1(l1), candidate.serializeIntentL1(l1),
                candidate.deserializeIntentL1(reference.serializeIntentL1(l1)), l1);
        compare("L2", reference.serializeIntentL2(l2), candidate.serializeIntentL2(l2),
                candidate.deserializeIntentL2(reference.serializeIntentL2(l2)), l2);
        compare("L3", reference.serializeIntentL3(l3), candidate.serializeIntentL3(l3),
                candidate.deserializeIntentL3(reference.serializeIntentL3(l3)), l3);
        compareFailure("L1 invalid", reference.deserializeIntentL1("{}"), candidate.deserializeIntentL1("{}"));
        compareFailure("L2 invalid", reference.deserializeIntentL2("{}"), candidate.deserializeIntentL2("{}"));
        compareFailure("L3 invalid", reference.deserializeIntentL3("{}"), candidate.deserializeIntentL3("{}"));
        compareFailure("Context invalid", reference.deserializeIntentExecutionContext("{}"),
                candidate.deserializeIntentExecutionContext("{}"));
        String expected = reference.serializeIntentExecutionContext(context);
        A4pResult<IntentExecutionContext> decoded = candidate.deserializeIntentExecutionContext(expected);
        if (!expected.equals(candidate.serializeIntentExecutionContext(context)) || !decoded.success()
                || !expected.equals(reference.serializeIntentExecutionContext(decoded.data()))) {
            throw new AssertionError("Context codec differs from standard");
        }
        System.out.println("Codec matches standard for these L1, L2, L3 and Context vectors");
    }

    private static <T> void compare(String label, String expectedJson, String actualJson,
                                    A4pResult<T> decoded, T expectedValue) {
        if (!expectedJson.equals(actualJson) || !decoded.success() || !expectedValue.equals(decoded.data())) {
            throw new AssertionError(label + " codec differs from standard");
        }
    }

    private static void compareFailure(String label, A4pResult<?> expected, A4pResult<?> actual) {
        if (actual.success() != expected.success() || actual.code() != expected.code()
                || actual.data() != null) {
            throw new AssertionError(label + " result differs from standard");
        }
    }
}
