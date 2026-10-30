package io.openjiuwen.a4p;

public enum A4pErrorCode {
    A4P_SUCCESS(0, "Operation completed successfully"),
    A4P_INVALID_PARAMETER(1001, "Invalid parameter"),
    A4P_UNSUPPORTED_VERSION(1002, "Unsupported version"),
    A4P_DUPLICATE_ID(1003, "Duplicate ID"),
    A4P_SIGNING_FAIL(2001, "Signing failed"),
    A4P_VERIFY_FAIL(2002, "Verification failed"),
    A4P_INVALID_STATUS(3001, "Invalid status"),
    A4P_TIMESTAMP_EXPIRED(3002, "Timestamp expired"),
    A4P_TIMESTAMP_TOO_EARLY(3003, "Timestamp too early"),
    A4P_EXECUTION_COUNT_EXCEEDED(4001, "Execution count exceeded"),
    A4P_AMOUNT_EXCEEDED(4002, "Amount exceeded"),
    // Retained for ArkTS error-code compatibility; Java uses 4004 for per-period execution limits.
    A4P_PERIOD_COUNT_EXCEEDED(4003, "Period count exceeded"),
    A4P_PERIOD_EXECUTION_COUNT_EXCEEDED(4004, "Period execution count exceeded"),
    A4P_CART_MISMATCH(4005, "Cart does not match the authorized resource"),
    A4P_SINGLE_ORDER_AMOUNT_EXCEEDED(4006, "Single order amount exceeded"),
    A4P_PRODUCT_NOT_AUTHORIZED(4007, "Product not authorized"),
    A4P_SERIALIZE_FAIL(5001, "Serialization failed"),
    A4P_DESERIALIZE_FAIL(5002, "Deserialization failed"),
    A4P_INTERNAL_ERROR(6001, "Unexpected internal error");

    private final int code;
    private final String message;

    A4pErrorCode(int code, String message) {
        this.code = code;
        this.message = message;
    }

    public int code() { return code; }
    public String message() { return message; }

    public static A4pErrorCode fromCode(int code) {
        for (A4pErrorCode value : values()) {
            if (value.code == code) {
                return value;
            }
        }
        return A4P_INTERNAL_ERROR;
    }
}
