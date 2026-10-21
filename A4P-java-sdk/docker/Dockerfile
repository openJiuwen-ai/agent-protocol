# Build environment for A4P Java SDK (linux/amd64 and linux/arm64).
# Build context: A4P-java-sdk/docker; mount the SDK source at /workspace.
ARG JAVA_VERSION=17
ARG MAVEN_VERSION=3.9
FROM maven:${MAVEN_VERSION}-eclipse-temurin-${JAVA_VERSION}

WORKDIR /workspace

# Fail early if the selected image does not provide the required toolchain.
RUN java -version && javac -version && mvn --version && bash --version

# Builds and tests must be invoked explicitly when starting the container.
CMD ["bash"]
