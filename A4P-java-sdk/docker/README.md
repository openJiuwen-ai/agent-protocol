# Java SDK Docker build environment

English | [简体中文](README_zh.md)

Run from the repository root. Docker must be able to pull the official Maven image;
Maven needs access to its dependency repositories on the first build.

```sh
docker build -t a4p-java-build:17 A4P-java-sdk/docker
docker run --rm -v "$PWD/A4P-java-sdk:/workspace" -v a4p-maven-cache:/root/.m2 a4p-java-build:17 bash build.sh build
docker run --rm -v "$PWD/A4P-java-sdk:/workspace" -v a4p-maven-cache:/root/.m2 a4p-java-build:17 bash build.sh ut
```

The container defaults to Bash and does not build the SDK automatically. Invoke
`bash build.sh build` explicitly to package `target/a4p-java-sdk-0.1.0.jar` without compiling or
running tests. The UT command writes reports to `target/surefire-reports/` and cleans
the previous JAR. Archive the JAR before running UT. Both commands return Maven's
exit status. Output files belong to the container user (root by default).

Use a newer JDK by selecting a Java version supported by the official Maven image:

```sh
docker build --build-arg JAVA_VERSION=21 -t a4p-java-build:21 A4P-java-sdk/docker
```

`MAVEN_VERSION` defaults to the `3.9` image tag and can be pinned to a published
3.9.x tag. The POM keeps `maven.compiler.release=17`, so builds with newer JDKs
still produce Java 17-compatible bytecode. The base image supplies native JDK and
Maven tools on both AMD64 and ARM64; CPU emulation is unnecessary.

For a custom repository mirror, mount a Maven settings file and pass it to the script:

```sh
docker run --rm -v "$PWD/A4P-java-sdk:/workspace" -v a4p-maven-cache:/root/.m2 \
  -v "$PWD/settings.xml:/tmp/settings.xml:ro" \
  a4p-java-build:17 bash build.sh build --settings /tmp/settings.xml
```

PowerShell: use `${PWD}/A4P-java-sdk` in place of `$PWD/A4P-java-sdk`.
