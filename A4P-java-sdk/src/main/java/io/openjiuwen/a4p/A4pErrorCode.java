package io.openjiuwen.a4p;

/**
 * Defines stable numeric protocol result codes and their fixed diagnostic messages.
 * Use numeric codes for application decisions rather than matching message text.
 */
public enum A4pErrorCode {
    /** Operation completed successfully. Numeric code: 0. */
    A4P_SUCCESS(0, "Operation completed successfully"),
    /** Invalid parameter. Numeric code: 1001. */
    A4P_INVALID_PARAMETER(1001, "Invalid parameter"),
    /** Unsupported version. Numeric code: 1002. */
    A4P_UNSUPPORTED_VERSION(1002, "Unsupported version"),
    /** Duplicate ID. Numeric code: 1003. */
    A4P_DUPLICATE_ID(1003, "Duplicate ID"),
    /** Signing failed. Numeric code: 2001. */
    A4P_SIGNING_FAIL(2001, "Signing failed"),
    /** Verification failed. Numeric code: 2002. */
    A4P_VERIFY_FAIL(2002, "Verification failed"),
    /** Invalid status. Numeric code: 3001. */
    A4P_INVALID_STATUS(3001, "Invalid status"),
    /** Timestamp expired. Numeric code: 3002. */
    A4P_TIMESTAMP_EXPIRED(3002, "Timestamp expired"),
    /** Timestamp too early. Numeric code: 3003. */
    A4P_TIMESTAMP_TOO_EARLY(3003, "Timestamp too early"),
    /** Execution count exceeded. Numeric code: 4001. */
    A4P_EXECUTION_COUNT_EXCEEDED(4001, "Execution count exceeded"),
    /** Amount exceeded. Numeric code: 4002. */
    A4P_AMOUNT_EXCEEDED(4002, "Amount exceeded"),
    // Retained for ArkTS error-code compatibility; Java uses 4004 for per-period execution limits.
    /** Period count exceeded. Numeric code: 4003. Reserved for ArkTS compatibility; Java uses code 4004 for periodic execution-count limits. */
    A4P_PERIOD_COUNT_EXCEEDED(4003, "Period count exceeded"),
    /** Period execution count exceeded. Numeric code: 4004. */
    A4P_PERIOD_EXECUTION_COUNT_EXCEEDED(4004, "Period execution count exceeded"),
    /** Cart does not match the authorized resource. Numeric code: 4005. Includes missing groups, repeated groups and incorrect template quantities. */
    A4P_CART_MISMATCH(4005, "Cart does not match the authorized resource"),
    /** Single order amount exceeded. Numeric code: 4006. */
    A4P_SINGLE_ORDER_AMOUNT_EXCEEDED(4006, "Single order amount exceeded"),
    /** Product not authorized. Numeric code: 4007. */
    A4P_PRODUCT_NOT_AUTHORIZED(4007, "Product not authorized"),
    /** Serialization failed. Numeric code: 5001. */
    A4P_SERIALIZE_FAIL(5001, "Serialization failed"),
    /** Deserialization failed. Numeric code: 5002. */
    A4P_DESERIALIZE_FAIL(5002, "Deserialization failed"),
    /** Unexpected internal error. Numeric code: 6001. */
    A4P_INTERNAL_ERROR(6001, "Unexpected internal error");

    /** Protocol result or exception error code. */
    private final int code;
    /** Diagnostic text associated with the result code. */
    private final String message;

    /**
     * Associates a stable numeric code with its diagnostic message.
     * @param code protocol result code; zero means success
     * @param message diagnostic message
     */
    A4pErrorCode(int code, String message) {
        this.code = code;
        this.message = message;
    }

    /**
     * Returns the stable numeric protocol code.
     * @return protocol result code
     */
    public int code() { return code; }
    /**
     * Returns the fixed English diagnostic message.
     * @return diagnostic message
     */
    public String message() { return message; }

    /**
     * Finds the enum constant for a numeric code; unknown codes map to A4P_INTERNAL_ERROR.
     * @param code protocol result code; zero means success
     * @return matching constant, or A4P_INTERNAL_ERROR for an unknown code
     */
    public static A4pErrorCode fromCode(int code) {
        for (A4pErrorCode value : values()) {
            if (value.code == code) {
                return value;
            }
        }
        return A4P_INTERNAL_ERROR;
    }
}
