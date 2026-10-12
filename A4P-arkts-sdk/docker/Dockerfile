# Build from the SDK root with -f docker/Dockerfile.
# Download Linux HarmonyOS 26.0.0 Command Line Tools from a ZIP archive.
# Replace the placeholder URL below or override COMMAND_LINE_TOOLS_URL.
ARG BASE_IMAGE=ubuntu:24.04
FROM ${BASE_IMAGE} AS toolchain

# 安装commandline-tools和模拟器的依赖
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       ca-certificates curl unzip git openjdk-17-jdk-headless libatomic1 libglib2.0-0 \
       libx11-6 libxext6 libxi6 libxrender1 libxtst6 libgl1 lsof \
       libpulse0 libegl1 libgbm1 libpng16-16 libfontconfig1 libfreetype6 \
       libxcb-icccm4 libxcb-image0 libxcb-keysyms1 libxcb-randr0 \
       libxcb-render-util0 libxcb-shape0 libxcb-xinerama0 libxcb-xkb1 \
       libsm6 libice6 libxkbcommon-x11-0 libxkbcommon0 \
    && rm -rf /var/lib/apt/lists/*
RUN ln -s "$(dirname "$(dirname "$(readlink -f /usr/bin/javac)")")" /opt/java

# 下载commandline-tools
ARG COMMAND_LINE_TOOLS_URL=https://openjiuwen-ci.obs.cn-north-4.myhuaweicloud.com/agent-protocol/tools/commandline-tools-linux-x64-26.0.0.851.zip
RUN curl --fail --location --retry 3 "${COMMAND_LINE_TOOLS_URL}" -o /tmp/command-line-tools.zip \
    && unzip -q /tmp/command-line-tools.zip -d /opt \
    && rm /tmp/command-line-tools.zip \
    && test -d /opt/command-line-tools/bin \
    && test -d /opt/command-line-tools/tool/node \
    && test -d /opt/command-line-tools/sdk \
    && test -d /opt/command-line-tools/emulator

ENV DEVECO_SDK_HOME=/opt/command-line-tools/sdk \
    OHOS_BASE_SDK_HOME=/opt/command-line-tools/sdk/default/openharmony \
    NODE_HOME=/opt/command-line-tools/tool/node \
    JAVA_HOME=/opt/java \
    EMULATOR_HOME=/opt/command-line-tools/emulator
ENV PATH="/opt/command-line-tools/bin:/opt/command-line-tools/tool/node/bin:/opt/command-line-tools/emulator:/opt/java/bin:${PATH}"

# 接受许可证并下载和创建模拟器实例
RUN mkdir -p /opt/emulator/images /opt/emulator/instances \
    && Emulator -license accept \
    && Emulator -install -deviceType phone \
       -osVersion "HarmonyOS 7.0.0(26.0.0)" \
       -imageRoot /opt/emulator/images \
    && Emulator -create A4pTestPhone -deviceType phone \
       -osVersion "HarmonyOS 7.0.0(26.0.0)" \
       -instancePath /opt/emulator/instances \
       -imageRoot /opt/emulator/images \
       -storage 8 -memory 16

# 启动脚本
COPY <<'EOF' /usr/local/bin/start-emulator.sh
#!/bin/bash
set -e
/opt/command-line-tools/emulator/Emulator \
    --start A4pTestPhone \
    -path /opt/emulator/instances \
    -imageRoot /opt/emulator/images \
    -noWindow &

for i in $(seq 1 60); do
    if hdc list targets 2>/dev/null | grep -q "127.0.0.1"; then
        echo "Emulator is ready."
        break
    fi
    sleep 5
done
exec tail -f /dev/null
EOF
RUN chmod +x /usr/local/bin/start-emulator.sh

# Mount the SDK source here when running build or test commands.
WORKDIR /workspace
CMD ["bash"]
