package io.openjiuwen.a4p;

/**
 * Reports a protocol error from throwing utility APIs or an unexpected SDK defect.
 * Lifecycle operations normally return protocol failures in A4pResult instead.
 * @see A4pResult
 */
public final class A4pException extends RuntimeException {
    /** Protocol result or exception error code. */
    private final A4pErrorCode code;

    /**
     * Constructs a protocol exception with a result code and diagnostic information.
     * @param code protocol result code; zero means success
     */
    public A4pException(A4pErrorCode code) {
        this(code, code.message(), null);
    }

    /**
     * Constructs a protocol exception with a result code and diagnostic information.
     * @param code protocol result code; zero means success
     * @param message diagnostic message
     * @param cause underlying cause, or null
     */
    public A4pException(A4pErrorCode code, String message, Throwable cause) {
        super(message, cause);
        this.code = code;
    }

    /**
     * Returns the protocol error represented by this exception.
     * @return protocol error enum constant
     */
    public A4pErrorCode code() { return code; }
}
