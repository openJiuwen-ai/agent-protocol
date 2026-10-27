#!/usr/bin/env bash
# A4P-Java-SDK 单元测试 CI 步骤。
#
# 使用 Maven Surefire 运行 JUnit 5 测试，生成标准格式报告。
#
# 产出:
#   - outputs/ut.log (Maven 测试日志)
#   - outputs/unit_test_report.html (HTML 报告)
#   - outputs/junit.xml (JUnit XML, 供 CI 解析)
#   - outputs/ut.json (状态汇总)
#
# 判定: mvn 退出码 0=PASS, 非0=FAILED。
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail || true

# ---------------------------------------------------------------------------
# 0. 参数解析
# ---------------------------------------------------------------------------
# 优先使用 WORKSPACE 环境变量，否则从脚本位置定位仓库根目录
if [ -n "${WORKSPACE:-}" ]; then
  BASE_DIR="$WORKSPACE"
else
  BASE_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
fi
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
echo "BASE_DIR: ${BASE_DIR}"

# ---------------------------------------------------------------------------
# 1. 必备工具
# ---------------------------------------------------------------------------
cd "$BASE_DIR"

# ---------------------------------------------------------------------------
# 2. 查找 Java 项目
# ---------------------------------------------------------------------------
JAVA_PROJECT_DIR=""
for dir in A4P-java-sdk a4p-java-sdk java-sdk; do
  if [ -d "$BASE_DIR/$dir" ] && [ -f "$BASE_DIR/$dir/pom.xml" ]; then
    JAVA_PROJECT_DIR="$BASE_DIR/$dir"
    break
  fi
done

if [ -z "$JAVA_PROJECT_DIR" ]; then
  echo "Error: No Java project with pom.xml found" >&2
  exit 1
fi

echo ">>> Found Java project: $JAVA_PROJECT_DIR"

# ---------------------------------------------------------------------------
# 3. 结果目录 (使用 BASE_DIR 而非 JAVA_PROJECT_DIR)
# ---------------------------------------------------------------------------
mkdir -p "$BASE_DIR/outputs"
HTML="$BASE_DIR/outputs/unit_test_report.html"
JUNIT="$BASE_DIR/outputs/junit.xml"
LOG="$BASE_DIR/outputs/ut.log"
JSON="$BASE_DIR/outputs/ut.json"
# 清除上次运行或随仓库检出的报告，避免失败时上传旧产物。
rm -f -- "$HTML" "$JUNIT" "$LOG" "$JSON"
m=""
for t in git mvn java; do
  command -v "$t" >/dev/null || m="$m $t"
done
[ -z "$m" ] || { echo "missing tools:$m" >&2; exit 1; }
echo ">>> Report output: $HTML"

# ---------------------------------------------------------------------------
# 4. 运行 Maven 测试
# ---------------------------------------------------------------------------
cd "$JAVA_PROJECT_DIR"

started_at=$(date +%s)
rc=0
mvn --batch-mode --no-transfer-progress clean test 2>&1 | tee "$LOG" || rc=${PIPESTATUS[0]}

# ---------------------------------------------------------------------------
# 5. 转换 Surefire 报告为标准 JUnit XML
# ---------------------------------------------------------------------------
# 使用 JDK 17 源文件启动模式生成自包含报告，无需额外依赖。
java -Dfile.encoding=UTF-8 "$SCRIPT_DIR/UtReport.java" \
  "$JAVA_PROJECT_DIR" "$HTML" "$JUNIT" "$LOG" "$started_at" "$rc"
echo ">>> Generated report: $HTML"

# ---------------------------------------------------------------------------
# 7. 判定通过/失败
# ---------------------------------------------------------------------------
if [ "$rc" -eq 0 ]; then
  st="PASS"
else
  st="FAILED"; echo ">>> ERROR: Maven exit code=$rc" >&2
fi

# ---------------------------------------------------------------------------
# 8. 汇总状态 JSON
# ---------------------------------------------------------------------------
PR_ID="${PR_ID:-0}"
OBS_BUCKET="${OBS_BUCKET:-openjiuwen-ci}"
OBS_PATH="agentcore/ut/${PR_ID}"
REPORT_LINK="https://${OBS_BUCKET}.obs.cn-north-4.myhuaweicloud.com/${OBS_PATH}/unit_test_report.html"
echo ">>> ut=$st (Maven exit=$rc)"
printf '{"status":"%s","exit_code":%d,"link":"%s"}\n' "$st" "$rc" "$REPORT_LINK" > "$JSON"

exit $rc
