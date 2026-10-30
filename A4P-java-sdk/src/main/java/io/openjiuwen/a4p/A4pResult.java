package io.openjiuwen.a4p;

import java.util.function.Supplier;
import static io.openjiuwen.a4p.A4pErrorCode.*;

/**
 * Represents a successful value or a protocol failure.
 * Check success() before accessing data(). Success has non-null data and code zero;
 * failure has null data and a nonzero code. SDK-generated messages are fixed diagnostics.
 * @param <T> successful payload type
 * @see A4pErrorCode
 */
public final class A4pResult<T> {
    /** Whether this result contains a successful payload. */
    private final boolean success;
    /** Successful business payload, or null on failure. */
    private final T data;
    /** Protocol result or exception error code. */
    private final long code;
    /** Diagnostic text associated with the result code. */
    private final String message;

    /**
     * Constructs a result with the supplied status, payload and diagnostic information.
     * @param success whether the result represents success
     * @param data successful payload, or null for a failure
     * @param code protocol result code; zero means success
     * @param message diagnostic message
     * @throws IllegalArgumentException if status, payload and numeric code are inconsistent
     */
    public A4pResult(boolean success, T data, long code, String message) {
        if (success ? data == null || code != A4P_SUCCESS.code()
                : data != null || code == A4P_SUCCESS.code()) {
            throw new IllegalArgumentException("Inconsistent A4pResult fields");
        }
        this.success = success;
        this.data = data;
        this.code = code;
        this.message = message;
    }

    /**
     * Constructs a result with the supplied status, payload and diagnostic information.
     * @param success whether the result represents success
     * @param data successful payload, or null for a failure
     * @param code protocol result code; zero means success
     */
    private A4pResult(boolean success, T data, A4pErrorCode code) {
        this.success = success;
        this.data = data;
        this.code = code.code();
        this.message = code.message();
    }

    /**
     * Builds a success result, mapping null payloads to an internal-error failure.
     * @param <T> successful payload type
     * @param data successful payload, or null for a failure
     * @return successful result, or internal-error failure for null data
     */
    static <T> A4pResult<T> ok(T data) {
        if (data == null) { return failure(A4P_INTERNAL_ERROR); }
        return new A4pResult<>(true, data, A4P_SUCCESS);
    }

    /**
     * Builds a failure result, mapping a missing or success error code to internal error.
     * @param <T> successful payload type
     * @param code protocol result code; zero means success
     * @return failure result with a nonzero error code
     */
    static <T> A4pResult<T> failure(A4pErrorCode code) {
        return new A4pResult<>(false, null, code == null || code == A4P_SUCCESS ? A4P_INTERNAL_ERROR : code);
    }

    /**
     * Reports whether this result contains a successful payload.
     * @return true if data is available
     */
    public boolean success() { return success; }
    /**
     * Returns the successful payload, or null on failure.
     * @return successful payload, or null
     */
    public T data() { return data; }
    /**
     * Returns the numeric protocol result code; zero indicates success.
     * @return protocol result code
     */
    public long code() { return code; }
    /**
     * Returns the diagnostic message associated with this result.
     * @return diagnostic message
     */
    public String message() { return message; }

    /**
     * Returns a diagnostic summary without including the business payload.
     * @return diagnostic summary excluding the business payload
     */
    @Override
    public String toString() {
        return "A4pResult[success=" + success + ", code=" + code + ", message=" + message + "]";
    }

    // Protocol failures become results; anything else is an SDK defect and surfaces as A4pException.
    /**
     * Executes an operation and converts protocol exceptions into failure results.
     * Wraps unexpected runtime exceptions as internal SDK defects rather than hiding them as protocol failures.
     * @param <T> successful payload type
     * @param action operation to execute
     * @return successful result or captured protocol failure
     */
    static <T> A4pResult<T> capture(Supplier<T> action) {
        try {
            return ok(action.get());
        } catch (A4pException error) {
            return failure(error.code());
        } catch (RuntimeException error) {
            throw new A4pException(A4P_INTERNAL_ERROR, A4P_INTERNAL_ERROR.message(), error);
        }
    }
}
