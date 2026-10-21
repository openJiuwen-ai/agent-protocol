# Java SDK Docker 构建环境

[English](README.md) | 简体中文

在仓库根目录执行以下命令。Docker 需要能够拉取官方 Maven 镜像；首次构建时 Maven 需要访问其依赖仓库。

```sh
docker build -t a4p-java-build:17 A4P-java-sdk/docker
docker run --rm -v "$PWD/A4P-java-sdk:/workspace" -v a4p-maven-cache:/root/.m2 a4p-java-build:17 bash build.sh build
docker run --rm -v "$PWD/A4P-java-sdk:/workspace" -v a4p-maven-cache:/root/.m2 a4p-java-build:17 bash build.sh ut
```

容器默认进入 Bash，不会自动构建 SDK。需要显式执行 `bash build.sh build`，在不编译、不运行测试的情况下打包出 `target/a4p-java-sdk-0.1.0.jar`。单元测试命令将报告写入 `target/surefire-reports/`，并清理上一次构建的 JAR，因此请在运行 UT 前归档 JAR。两条命令均返回 Maven 的退出状态。产物文件归属容器用户（默认为 root）。

## 更换 JDK 版本

选择官方 Maven 镜像支持的 Java 版本，即可使用更新的 JDK：

```sh
docker build --build-arg JAVA_VERSION=21 -t a4p-java-build:21 A4P-java-sdk/docker
```

`MAVEN_VERSION` 默认为 `3.9` 镜像标签，也可以固定为已发布的 3.9.x 标签。POM 中保持 `maven.compiler.release=17`，因此使用更新的 JDK 构建时仍生成兼容 Java 17 的字节码。基础镜像在 AMD64 和 ARM64 上均提供原生 JDK 与 Maven 工具，无需 CPU 模拟。

## 使用自定义仓库镜像

挂载 Maven settings 文件并将其传给脚本：

```sh
docker run --rm -v "$PWD/A4P-java-sdk:/workspace" -v a4p-maven-cache:/root/.m2 \
  -v "$PWD/settings.xml:/tmp/settings.xml:ro" \
  a4p-java-build:17 bash build.sh build --settings /tmp/settings.xml
```

PowerShell：将 `$PWD/A4P-java-sdk` 替换为 `${PWD}/A4P-java-sdk`。
