#!/usr/bin/env bash
# CI entry point with separate build and unit-test modes.
set -euo pipefail

usage() {
  echo "Usage: bash build.sh [build|ut] [additional Maven options]"
  echo "  build  Compile and package the JAR without compiling/running tests (default)."
  echo "  ut     Compile and run unit tests without packaging the JAR."
}

MODE="${1:-build}"
if [[ $# -gt 0 ]]; then
  shift
fi
case "${MODE}" in
  build)
    MAVEN_ARGS=(-Dmaven.test.skip=true clean package)
    ;;
  ut)
    MAVEN_ARGS=(-DskipTests=false -Dmaven.test.skip=false -Dmaven.test.failure.ignore=false -DfailIfNoTests=true clean test)
    ;;
  -h|--help)
    usage
    exit 0
    ;;
  *)
    echo "Error: unknown mode '${MODE}'." >&2
    usage >&2
    exit 2
    ;;
esac

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
# 流水线 Shell 步骤在检出的仓库根目录执行。
BASE_DIR="${WORKSPACE:-$PWD}"
if ! command -v mvn >/dev/null 2>&1; then
  echo "Error: Maven 3.9+ is required; install Maven and add mvn to PATH." >&2
  exit 1
fi

# Resolve the project independently of the caller's working directory.
# Preserve Maven's exit code so compilation/test failures fail the CI job.
exec mvn --batch-mode --no-transfer-progress --show-version \
  --file "${PROJECT_DIR}/pom.xml" "$@" "${MAVEN_ARGS[@]}"

# --- 6. obsutil 安装 (若未预装) -------------------------------------------
if ! command -v obsutil >/dev/null; then
    echo "obsutil 未安装，开始自动安装..."
    mkdir -p "$BASE_DIR/out/codearts"
    cd "$BASE_DIR/out/codearts"
    wget https://obs-community.obs.cn-north-1.myhuaweicloud.com/obsutil/current/obsutil_linux_amd64.tar.gz -O obsutil.tar.gz
    tar -zxf obsutil.tar.gz
    cd obsutil_* || { echo "解压失败"; exit 1; }
    mkdir -p "$BUILD_HOME/.local/bin"
    cp obsutil "$BUILD_HOME/.local/bin/"
    export PATH="$BUILD_HOME/.local/bin:$PATH"
    command -v obsutil >/dev/null || { echo "obsutil 安装失败"; exit 1; }
    cd "$BASE_DIR"
fi

# --- 7. 上传 wheel + md5 到 OBS -------------------------------------------
OBS_AK="${OBS_AK}"; OBS_SK="${OBS_SK}"
OBS_BUCKET="openjiuwen-ci"; OBS_REGION="cn-north-4"
OBS_PATH="agent-protocol/package/${PR_ID:-0}/"
obsutil config -i "$OBS_AK" -k "$OBS_SK" -e "obs.cn-north-4.myhuaweicloud.com"
