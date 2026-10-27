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
# 优先使用 WORKSPACE 环境变量，否则使用脚本所在目录
if [ -n "${WORKSPACE:-}" ]; then
  BASE_DIR="$WORKSPACE"
elif [ -d "$(dirname "$0")" ]; then
  cd "$(dirname "$0")"; BASE_DIR="$PWD"
else
  cd "$(dirname "$0")/.."; BASE_DIR="$PWD"
fi
echo "BASE_DIR: ${BASE_DIR}"

# ---------------------------------------------------------------------------
# 1. 必备工具
# ---------------------------------------------------------------------------
m=""
for t in git mvn; do
  command -v "$t" >/dev/null || m="$m $t"
done
[ -z "$m" ] || { echo "missing tools:$m" >&2; exit 1; }

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

# ---------------------------------------------------------------------------
# 4. 运行 Maven 测试
# ---------------------------------------------------------------------------
cd "$JAVA_PROJECT_DIR"

rc=0
mvn --batch-mode --no-transfer-progress clean test 2>&1 | tee "$LOG" || rc=${PIPESTATUS[0]}

# ---------------------------------------------------------------------------
# 5. 转换 Surefire 报告为标准 JUnit XML
# ---------------------------------------------------------------------------
total_tests=0
failures=0
errors=0
skipped=0

if [ -d "target/surefire-reports" ] && [ -n "$(ls -A target/surefire-reports/*.xml 2>/dev/null)" ]; then
  > "$JUNIT"
  echo '<?xml version="1.0" encoding="UTF-8"?>' > "$JUNIT"
  echo '<testsuites>' >> "$JUNIT"

  for xml_file in target/surefire-reports/TEST-*.xml; do
    if [ -f "$xml_file" ]; then
      ts_tests=$(grep -oP 'tests="?\K[0-9]+' "$xml_file" | head -1 || echo 0)
      ts_failures=$(grep -oP 'failures="?\K[0-9]+' "$xml_file" | head -1 || echo 0)
      ts_errors=$(grep -oP 'errors="?\K[0-9]+' "$xml_file" | head -1 || echo 0)
      ts_skipped=$(grep -oP 'skipped="?\K[0-9]+' "$xml_file" | head -1 || echo 0)

      total_tests=$((total_tests + ts_tests))
      failures=$((failures + ts_failures))
      errors=$((errors + ts_errors))
      skipped=$((skipped + ts_skipped))

      sed -n '2,$p' "$xml_file" | sed '$d' >> "$JUNIT"
    fi
  done

  echo '</testsuites>' >> "$JUNIT"
fi

# ---------------------------------------------------------------------------
# 6. 生成 HTML 报告
# ---------------------------------------------------------------------------
{
  echo "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>Unit Test Report</title>"
  echo "<style>"
  echo "body{font-family:Arial,sans-serif;margin:20px}"
  echo ".pass{color:green;font-weight:bold}"
  echo ".fail{color:red;font-weight:bold}"
  echo "pre{background:#f5f5f5;padding:10px;overflow-x:auto;max-height:500px}"
  echo ".summary{background:#f0f0f0;padding:15px;border-radius:5px;margin:20px 0}"
  echo "</style>"
  echo "</head><body>"
  echo "<h1>Java Unit Test Report (Maven Surefire)</h1>"
  echo "<div class='summary'>"
  echo "<p>Total tests: <strong>$total_tests</strong></p>"
  echo "<p>Failures: <span class='fail'>$failures</span></p>"
  echo "<p>Errors: <span class='fail'>$errors</span></p>"
  echo "<p>Skipped: $skipped</p>"
  echo "</div>"
  echo "<hr><h2>Test Log (last 200 lines)</h2>"
  echo "<pre>"
  tail -n 200 "$LOG"
  echo "</pre></body></html>"
} > "$HTML"

# ---------------------------------------------------------------------------
# 7. 判定通过/失败
# ---------------------------------------------------------------------------
if [ "$rc" -eq 0 ]; then
  st="PASS"
elif [ "$rc" -eq 0 ] && [ "$failures" -gt 0 ] || [ "$errors" -gt 0 ]; then
  st="FAILED"; echo ">>> ERROR: test failures or errors detected" >&2
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