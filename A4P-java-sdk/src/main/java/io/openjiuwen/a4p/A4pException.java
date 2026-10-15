package io.openjiuwen.a4p;

public final class A4pException extends RuntimeException {
    private final A4pErrorCode code;

    public A4pException(A4pErrorCode code) {
        this(code, code.message(), null);
    }

    public A4pException(A4pErrorCode code, String message, Throwable cause) {
        super(message, cause);
        this.code = code;
    }

    public A4pErrorCode code() { return code; }
}
