# Build from the SDK root with -f docker/Dockerfile.
# Download Linux HarmonyOS 26.0.0 Command Line Tools from a ZIP archive.
# Replace the placeholder URL below or override COMMAND_LINE_TOOLS_URL.
ARG BASE_IMAGE=ubuntu:24.04
FROM ${BASE_IMAGE} AS toolchain

# libgl1 is needed by the SDK resource compiler; lsof by the local UT runner.
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       ca-certificates curl unzip git openjdk-17-jdk-headless libatomic1 libglib2.0-0 \
       libx11-6 libxext6 libxi6 libxrender1 libxtst6 libgl1 lsof \
    && rm -rf /var/lib/apt/lists/*
RUN ln -s "$(dirname "$(dirname "$(readlink -f /usr/bin/javac)")")" /opt/java

# Placeholder: replace with the actual Linux Command Line Tools ZIP URL.
ARG COMMAND_LINE_TOOLS_URL=https://openjiuwen-ci.obs.cn-north-4.myhuaweicloud.com/agent-protocol/tools/commandline-tools-linux-x64-26.0.0.851.zip
RUN curl --fail --location --retry 3 "${COMMAND_LINE_TOOLS_URL}" -o /tmp/command-line-tools.zip \
    && unzip -q /tmp/command-line-tools.zip -d /opt \
    && rm /tmp/command-line-tools.zip \
    && test -d /opt/command-line-tools/bin \
    && test -d /opt/command-line-tools/tool/node \
    && test -d /opt/command-line-tools/sdk
ENV DEVECO_SDK_HOME=/opt/command-line-tools/sdk \
    OHOS_BASE_SDK_HOME=/opt/command-line-tools/sdk/default/openharmony \
    NODE_HOME=/opt/command-line-tools/tool/node \
    JAVA_HOME=/opt/java
ENV PATH="/opt/command-line-tools/bin:/opt/command-line-tools/tool/node/bin:/opt/java/bin:${PATH}"

# Mount the SDK source here when running build or test commands.
WORKDIR /workspace
CMD ["bash"]
