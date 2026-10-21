package io.openjiuwen.a4p;

import java.util.function.Supplier;
import static io.openjiuwen.a4p.A4pErrorCode.*;

public final class A4pResult<T> {
    private final boolean success;
    private final T data;
    private final long code;
    private final String message;

    private A4pResult(boolean success, T data, A4pErrorCode code) {
        this.success = success;
        this.data = data;
        this.code = code.code();
        this.message = code.message();
    }

    public static <T> A4pResult<T> ok(T data) {
        if (data == null) { return failure(A4P_INTERNAL_ERROR); }
        return new A4pResult<>(true, data, A4P_SUCCESS);
    }

    public static <T> A4pResult<T> failure(A4pErrorCode code) {
        return new A4pResult<>(false, null, code == null || code == A4P_SUCCESS ? A4P_INTERNAL_ERROR : code);
    }

    public boolean success() { return success; }
    public T data() { return data; }
    public long code() { return code; }
    public String message() { return message; }

    @Override
    public String toString() {
        return "A4pResult[success=" + success + ", code=" + code + ", message=" + message + "]";
    }

    // Protocol failures become results; anything else is an SDK defect and surfaces as A4pException.
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
