# A4P Java SDK

[Public API and usage contract](docs/API.md) | [公开 API 与使用约束](docs/API_zh.md)

English | [简体中文](./README_zh.md)

Java 17 implementation of the A4P payment-intent protocol.

## Quick build

```sh
bash A4P-java-sdk/scripts/build.sh build    # Explicitly compile and package
```

Build artifacts are located at `A4P-java-sdk/target/a4p-java-sdk-*.jar`.

## Build and test

Requirements: JDK 17 or later, Maven 3.9 or later.

```sh
cd A4P-java-sdk
mvn test
mvn package
```

### Generate English API documentation

From the repository root, run:

```sh
mvn -f A4P-java-sdk/pom.xml javadoc:javadoc
```

Open `A4P-java-sdk/target/site/apidocs/index.html` to browse the English documentation.
A full JDK 17+ (including `javadoc`) and Maven 3.9+ are required; the first run downloads plugins.
Generated pages cover the public API (including nested types), excluding tests and the standalone examples module.
Missing comments do not prevent the build; malformed Javadoc or broken references cause generation to fail.

`mvn package` automatically generates and attaches the English `target/a4p-java-sdk-0.1.0-javadoc.jar`.
To run tests, package the SDK, and generate HTML documentation together:

```sh
mvn -f A4P-java-sdk/pom.xml clean verify javadoc:javadoc
```

When changing an API, update its English source comments in the same change. When public usage or build instructions change,
also update `docs/API.md`, `docs/API_zh.md`, and the corresponding READMEs.
Generated files under `target/` do not need to be edited manually or committed.

### Build script

The script requires Bash (for example, Linux, WSL, or Git Bash on Windows). From the repository root, run:

```sh
bash A4P-java-sdk/scripts/build.sh          # Default: build mode
bash A4P-java-sdk/scripts/build.sh build    # Explicitly compile and package
bash A4P-java-sdk/scripts/build.sh ut       # Run unit tests only
bash A4P-java-sdk/scripts/build.sh docs     # Generate Javadoc documentation
bash A4P-java-sdk/scripts/build.sh --help
```

- `build` runs `clean package` with `-Dmaven.test.skip=true`, skipping both compilation and execution of test code.
  The artifact is `A4P-java-sdk/target/a4p-java-sdk-0.1.0.jar`.
- `ut` runs `clean test`, requires tests to be present, and returns a failure if tests fail. It does not package a JAR;
  Surefire test reports are located at `A4P-java-sdk/target/surefire-reports/`.
- `docs` generates documentation using Javadoc.

All modes clean `target/` first and return Maven's exit code.

Place additional Maven arguments after the script's mode selection, for example:

```sh
bash A4P-java-sdk/scripts/build.sh build -o
mvn -f A4P-java-sdk/examples/pom.xml test
```

`-o` enables Maven's offline mode; required dependencies and plugins must already be cached. The script locates `pom.xml`
relative to itself, independently of the caller's working directory.

### UT script with CI reports

To run unit tests and generate CI reports, run the following command from the repository root. The script locates the
repository root relative to itself, independently of the caller's working directory. You can override the default
by setting `WORKSPACE` to the absolute path of the repository root.

```sh
bash A4P-java-sdk/scripts/ut.sh
```

The script requires Git, Maven, and standard shell utilities, including `grep` with `-P` support (for example, GNU grep).
It runs `mvn clean test` and returns Maven's exit code (`0` for `PASS`, nonzero for `FAILED`).
Reports are written to `outputs/` under the resolved repository root (`$WORKSPACE/outputs/` when overridden):

| File | Contents |
| --- | --- |
| `ut.log` | Maven test log |
| `unit_test_report.html` | Test counts and the last 200 log lines |
| `junit.xml` | Combined JUnit XML, generated only when Surefire XML reports exist |
| `ut.json` | Status, Maven exit code, and report links |

Optional environment variables `PR_ID` (default `0`) and `OBS_BUCKET` (default `openjiuwen-ci`) configure
report links in `ut.json`. The script only generates local reports; CI must upload them separately to the linked OBS locations.

### Build dependencies

The artifact coordinates are `io.openjiuwen:a4p-java-sdk:0.1.0`. Jackson handles JSON; JUnit 5 is used only for testing. No web framework is required.

`jackson-databind` (2.20.1 here) is the only runtime dependency; JUnit 5 has `test` scope.

## Run the example program

The example program depends on the external Java SDK:

```xml
<dependency>
  <groupId>io.openjiuwen</groupId>
  <artifactId>a4p-java-sdk</artifactId>
  <version>0.1.0</version>
</dependency>
```

First run `mvn -f A4P-java-sdk/pom.xml install` to build the SDK, then run `io.openjiuwen.a4p.examples.PaymentExample` from the standalone examples module:

```sh
mvn -f A4P-java-sdk/examples/pom.xml compile org.codehaus.mojo:exec-maven-plugin:3.6.3:java "-Dexec.mainClass=io.openjiuwen.a4p.examples.PaymentExample"
```

The exec plugin is not declared in `pom.xml`, so this command resolves it from Maven Central on first use and requires network access.

The example uses separate Agent, intent center, and user RSA-3072 key pairs to demonstrate creating, issuing, authorizing, activating, verifying, reserving, and completing a payment.

## API

For detailed API documentation, see [docs/API.md](docs/API.md).

To use the classes and functions defined in the SDK, import:

```java
import io.openjiuwen.a4p.*;
import io.openjiuwen.a4p.Models.*;
```

Build AI_PAY payloads through the `PaymentProfile` helper class. The following snippet illustrates the calls; see
`io.openjiuwen.a4p.examples.PaymentExample` for a complete, compilable version that also sets up the key pairs and the L1/L2/L3 chain.

```java
// Activate an intent created by the Agent and issued and signed by the intent center.
A4pResult<IntentExecutionContext> activated = A4p.activateIntent(authorizedIntent, userVerifier, now);
if (!activated.success()) { /* Handle according to activated.code() */ }
IntentExecutionContext context = activated.data();

// Reserve an order against the activated intent, then complete it.
Order order = new Order("order-1", 3000L, "CNY", List.of(new CartItem(product, 1L)), now);
Execution execution = PaymentProfile.execution("execution-1", order);
A4pResult<IntentExecutionContext> reserved = A4p.createExecution(context, execution, now);
if (!reserved.success()) { /* Handle according to reserved.code(), then persist context */ }
A4pResult<IntentExecutionContext> completed = A4p.completeExecution(context, execution.id(), now);

PaymentExecutionInfo info = PaymentProfile.executionInfo(context);
```

### Result codes

The following values come from `A4pErrorCode`, shared with the ArkTS SDK. `0` indicates success; any other value indicates a protocol failure.
`A4pErrorCode.fromCode(int)` resolves a code back to its constant;
unknown values map to `A4P_INTERNAL_ERROR` (cast the `long` returned by `code()` to `int` when calling it).

| Code | Constant | Meaning |
| --- | --- | --- |
| 0 | `A4P_SUCCESS` | Operation completed successfully |
| 1001 | `A4P_INVALID_PARAMETER` | Invalid parameter |
| 1002 | `A4P_UNSUPPORTED_VERSION` | Unsupported version |
| 1003 | `A4P_DUPLICATE_ID` | Duplicate ID |
| 2001 | `A4P_SIGNING_FAIL` | Signing failed |
| 2002 | `A4P_VERIFY_FAIL` | Verification failed |
| 3001 | `A4P_INVALID_STATUS` | Invalid status |
| 3002 | `A4P_TIMESTAMP_EXPIRED` | Timestamp expired |
| 3003 | `A4P_TIMESTAMP_TOO_EARLY` | Timestamp too early |
| 4001 | `A4P_EXECUTION_COUNT_EXCEEDED` | Execution count limit exceeded |
| 4002 | `A4P_AMOUNT_EXCEEDED` | Amount limit exceeded |
| 4003 | `A4P_PERIOD_COUNT_EXCEEDED` | Reserved for ArkTS error-code compatibility; Java does not currently return this code (per-period execution count limits use 4004) |
| 4004 | `A4P_PERIOD_EXECUTION_COUNT_EXCEEDED` | Per-period execution count limit exceeded |
| 4005 | `A4P_CART_MISMATCH` | Cart does not match the authorized order template |
| 4006 | `A4P_SINGLE_ORDER_AMOUNT_EXCEEDED` | Single-order amount limit exceeded |
| 4007 | `A4P_PRODUCT_NOT_AUTHORIZED` | Product not authorized |
| 5001 | `A4P_SERIALIZE_FAIL` | Serialization failed |
| 5002 | `A4P_DESERIALIZE_FAIL` | Deserialization failed |
| 6001 | `A4P_INTERNAL_ERROR` | Unexpected internal error |

## Version and compatibility

- Current protocol version: `1.0` (`A4p.PROTOCOL_VERSION`)
- Java SDK build version: `0.1.0`

## More documentation

Reference documents are located in `docs/`:

| Document | Contents |
| --- | --- |
| [API.md](docs/API.md) | Detailed API documentation |
| [serialization_and_signature_rules.md](docs/serialization_and_signature_rules.md) | Serialization and signature rules |

## Tests

Tests cover lifecycle, quota rollback, order idempotency, one-to-one cart matching, check ordering, expiry finalization,
terminal states, consumption, whitelist validation, period boundaries, blank and structural validation, the result model,
typed-union payloads, wire-format names and enum encoding, concurrent reservations, canonical vectors, detached signature
metadata, malformed input, verifier result consistency, execution-context persistence and restoration (including reachable
states and record timestamps), and real RSA signature verification and tampering detection. The digest signer in the test
fixtures is only a deterministic test double and must not be used for authorization.
