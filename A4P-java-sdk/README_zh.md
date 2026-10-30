# A4P Java SDK

[公开 API 与使用约束](docs/API_zh.md) | [Public API and usage contract](docs/API.md)

[English](./README.md) | 简体中文

A4P 支付意图协议的 Java 17 实现

## 快速构建

```sh
bash A4P-java-sdk/scripts/build.sh build    # 显式指定编译打包
```

构建产物位于 A4P-java-sdk/target/a4p-java-sdk-*.jar

## 构建与测试

环境要求：JDK 17 及以上、Maven 3.9 及以上。

```sh
cd A4P-java-sdk
mvn test
mvn package
```

### 生成英文 API 文档

在仓库根目录执行：

```sh
mvn -f A4P-java-sdk/pom.xml javadoc:javadoc
```

打开 `A4P-java-sdk/target/site/apidocs/index.html` 浏览英文文档。
需要完整 JDK 17+（含 `javadoc`）及 Maven 3.9+；首次执行需要下载插件。
生成页面展示公开 API（含嵌套类型），不包含测试和独立示例模块。
缺失注释不会阻止构建；错误的 Javadoc 语法或失效引用会使生成失败。

`mvn package` 会自动生成并附带英文 `target/a4p-java-sdk-0.1.0-javadoc.jar`。
同时执行测试、SDK 打包及 HTML 生成：

```sh
mvn -f A4P-java-sdk/pom.xml clean verify javadoc:javadoc
```

修改 API 时，在同一次变更中同步更新英文源码注释；公开用法或构建方式变化时，
再更新 `docs/API.md`、`docs/API_zh.md` 和相应 README。
`target/` 下的生成文件无需手工修改或提交。

### 编译脚本

脚本需要 Bash 环境（例如 Linux、WSL 或 Windows 上的 Git Bash）。在仓库根目录执行：

```sh
bash A4P-java-sdk/scripts/build.sh          # 默认使用 build 模式
bash A4P-java-sdk/scripts/build.sh build    # 显式指定编译打包
bash A4P-java-sdk/scripts/build.sh ut       # 仅运行单元测试
bash A4P-java-sdk/scripts/build.sh docs     # 生成 javadoc 文档
bash A4P-java-sdk/scripts/build.sh --help
```

- `build` 使用 `-Dmaven.test.skip=true` 执行 `clean package`，跳过测试代码的编译与执行，
产物为 `A4P-java-sdk/target/a4p-java-sdk-0.1.0.jar`。
- `ut` 执行 `clean test`，要求存在测试，测试失败时返回失败，不打包 JAR；Surefire 测试报告位于
`A4P-java-sdk/target/surefire-reports/`。
- `docs` 使用 javadoc 生成文档

所有模式都会先清理 `target/`，并返回 Maven 的退出码。

额外的 Maven 参数放在脚本选择运行模式之后，例如：

```sh
bash A4P-java-sdk/scripts/build.sh build -o
mvn -f A4P-java-sdk/examples/pom.xml test
```

`-o` 表示 Maven 离线模式，所需依赖和插件必须已缓存。脚本根据自身位置定位 `pom.xml`，
不依赖调用时的工作目录。

### 生成 CI 报告的 UT 脚本

如需运行单元测试并生成 CI 报告，在仓库根目录执行以下命令。脚本根据自身位置定位仓库
根目录，不依赖调用者的工作目录；也可通过 `WORKSPACE` 指定仓库根目录的绝对路径来覆盖默认值。

```sh
bash A4P-java-sdk/scripts/ut.sh
```

该脚本需要 Git、Maven 和常用 Shell 工具，其中 `grep` 需要支持 `-P`（例如 GNU grep）。
脚本执行 `mvn clean test`，并返回 Maven 的退出码（`0` 为 `PASS`，非 `0` 为 `FAILED`）。
报告输出到定位到的仓库根目录下的 `outputs/`（设置覆盖值时为 `$WORKSPACE/outputs/`）：

| 文件 | 内容 |
| --- | --- |
| `ut.log` | Maven 测试日志 |
| `unit_test_report.html` | 测试数量统计及最后 200 行日志 |
| `junit.xml` | 合并后的 JUnit XML，仅在存在 Surefire XML 报告时生成 |
| `ut.json` | 状态、Maven 退出码及报告链接 |

可选环境变量 `PR_ID`（默认 `0`）与 `OBS_BUCKET`（默认 `openjiuwen-ci`）用于配置
`ut.json` 中的报告链接。脚本只生成本地报告，上传到链接对应的 OBS 位置需由 CI 另行完成。

### 构建依赖

构件坐标为 `io.openjiuwen:a4p-java-sdk:0.1.0`。JSON 由 Jackson 处理；JUnit 5 仅用于测试。不需要任何 Web 框架。

`jackson-databind`（此处为 2.20.1）是唯一的运行时依赖，JUnit 5 为 `test` 作用域。

## 运行示例程序

示例程序依赖外部的 java SDK

```xml
<dependency>
  <groupId>io.openjiuwen</groupId>
  <artifactId>a4p-java-sdk</artifactId>
  <version>0.1.0</version>
</dependency>
```

先运行 `mvn -f A4P-java-sdk/pom.xml install` 构建 SDK，再从独立示例模块运行 `io.openjiuwen.a4p.examples.PaymentExample`：

```sh
mvn -f A4P-java-sdk/examples/pom.xml compile org.codehaus.mojo:exec-maven-plugin:3.6.3:java "-Dexec.mainClass=io.openjiuwen.a4p.examples.PaymentExample"
```

`pom.xml` 中并未声明 exec 插件，因此该命令首次执行时会从 Maven 中央仓库解析该插件，需要有网络访问。

示例使用相互独立的 Agent、意图中心（intent center）与用户 RSA-3072 密钥对，演示了创建、签发、授权、激活、验签、预留与完成一笔支付。

## API

详细的 API 请见 [[docs/API_zh.md]]

若要使用 SDK 中定义的类和函数，用以下方式导入：

```java
import io.openjiuwen.a4p.*;
import io.openjiuwen.a4p.Models.*;
```

AI_PAY 载荷请通过 `PaymentProfile` 辅助类构造。下面的片段展示调用形态；完整可编译的版本见
`io.openjiuwen.a4p.examples.PaymentExample`，其中还包含密钥对、L1/L2/L3 链的搭建过程。

```java
// 激活一个由 Agent 创建、意图中心签发并签名的意图。
A4pResult<IntentExecutionContext> activated = A4p.activateIntent(authorizedIntent, userVerifier, now);
if (!activated.success()) { /* 依据 activated.code() 分支处理 */ }
IntentExecutionContext context = activated.data();

// 针对已激活的意图预留一笔订单，然后完成它。
Order order = new Order("order-1", 3000L, "CNY", List.of(new CartItem(product, 1L)), now);
Execution execution = PaymentProfile.execution("execution-1", order);
A4pResult<IntentExecutionContext> reserved = A4p.createExecution(context, execution, now);
if (!reserved.success()) { /* 依据 reserved.code() 分支处理，然后持久化 context */ }
A4pResult<IntentExecutionContext> completed = A4p.completeExecution(context, execution.id(), now);

PaymentExecutionInfo info = PaymentProfile.executionInfo(context);
```

### 结果码

下面的取值来自与 ArkTS SDK 共用的 `A4pErrorCode`。`0` 表示成功，其他任何值都是协议失败。
`A4pErrorCode.fromCode(int)` 把码还原为常量，
未知值映射为 `A4P_INTERNAL_ERROR`（调用时需把 `code()` 返回的 `long` 转成 `int`）。

| 码 | 常量 | 含义 |
| --- | --- | --- |
| 0 | `A4P_SUCCESS` | 操作成功 |
| 1001 | `A4P_INVALID_PARAMETER` | 参数无效 |
| 1002 | `A4P_UNSUPPORTED_VERSION` | 版本不受支持 |
| 1003 | `A4P_DUPLICATE_ID` | ID 重复 |
| 2001 | `A4P_SIGNING_FAIL` | 签名失败 |
| 2002 | `A4P_VERIFY_FAIL` | 验证失败 |
| 3001 | `A4P_INVALID_STATUS` | 状态无效 |
| 3002 | `A4P_TIMESTAMP_EXPIRED` | 时间戳已过期 |
| 3003 | `A4P_TIMESTAMP_TOO_EARLY` | 时间戳过早 |
| 4001 | `A4P_EXECUTION_COUNT_EXCEEDED` | 超出执行次数上限 |
| 4002 | `A4P_AMOUNT_EXCEEDED` | 超出金额上限 |
| 4003 | `A4P_PERIOD_COUNT_EXCEEDED` | 为兼容 ArkTS 错误码而保留；Java 当前不返回此码（周期执行次数超限使用 4004） |
| 4004 | `A4P_PERIOD_EXECUTION_COUNT_EXCEEDED` | 超出周期执行次数上限 |
| 4005 | `A4P_CART_MISMATCH` | 购物车不符合授权订单模板 |
| 4006 | `A4P_SINGLE_ORDER_AMOUNT_EXCEEDED` | 超出单笔订单金额上限 |
| 4007 | `A4P_PRODUCT_NOT_AUTHORIZED` | 商品未被授权 |
| 5001 | `A4P_SERIALIZE_FAIL` | 序列化失败 |
| 5002 | `A4P_DESERIALIZE_FAIL` | 反序列化失败 |
| 6001 | `A4P_INTERNAL_ERROR` | 意外的内部错误 |

## 版本与兼容性

- 当前协议版本: `1.0`（`A4p.PROTOCOL_VERSION`）
- java SDK 构建版本: `0.1.0`

## 更多文档

参考文档位于 `docs/` 目录：

| 文档 | 内容 |
| --- | --- |
| [API_zh.md](docs/API_zh.md) | API 详细说明 |
| [serialization_and_signature_rules_zh.md](docs/serialization_and_signature_rules_zh.md) | 序列化与签名规则 |

## 测试

测试覆盖生命周期、配额回滚、订单幂等、购物车一一匹配、检查顺序、过期终结、终态、消费、白名单校验、
周期边界、空白与结构校验、结果模型、类型联合载荷、线格式名称与枚举编码、并发预留、canonical 向量、
分离的签名元数据、畸形输入、验证器结果一致性、执行 context 的持久化与恢复（包括状态可达性与记录
时间戳），以及真实的 RSA 签名验证与篡改检测。测试夹具中的 digest 签名器只是一个确定性的测试替身，
不得用于授权。
