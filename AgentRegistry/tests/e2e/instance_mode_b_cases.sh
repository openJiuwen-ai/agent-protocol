#!/usr/bin/env bash
# 实例双模式（模式 B：registry 直连元戎）curl 测试用例。
#
# TDD：本脚本先于实现编写，Phase 3/4/5 验收以本脚本全部 PASS 为准。
#
# 自包含：脚本自行拉起 元戎 mock（blackbox/tools/mock_yuanrong.py）+ 注册中心
# （memory 后端、appliance 模式），跑完清理。502/504（元戎失败/超时）与强并发
# 409 断言由 pytest 覆盖（mock 不支持失败注入），本脚本只做黑盒 HTTP 契约。
#
# 用法：
#   tests/e2e/instance_mode_b_cases.sh          # 默认端口 18321/18322/18323
#   REG_PORT=19001 MOCK_PORT=19002 tests/e2e/instance_mode_b_cases.sh
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PY="$ROOT/.venv/bin/python"
BLACKBOX="${BLACKBOX:-/home/dyc/code/blackbox}"
MOCK_SCRIPT="$BLACKBOX/tools/mock_yuanrong.py"

REG_PORT="${REG_PORT:-18321}"
MOCK_PORT="${MOCK_PORT:-18322}"
REG_PORT_B="${REG_PORT_B:-18323}"   # 未配元戎的第二个注册中心（501 用例）
BASE="http://127.0.0.1:${REG_PORT}"
BASE_B="http://127.0.0.1:${REG_PORT_B}"

TMP="$(mktemp -d /tmp/registry-e2e-XXXXXX)"
PASS=0; FAIL=0
PIDS=()

cleanup() {
    for pid in "${PIDS[@]:-}"; do kill "$pid" 2>/dev/null; done
    rm -rf "$TMP"
}
trap cleanup EXIT

# ── 断言工具 ───────────────────────────────────────────────────────
req() { # req <BASE> <METHOD> <PATH> [JSON]
    local base="$1" method="$2" path="$3" data="${4:-}"
    if [ -n "$data" ]; then
        HTTP_CODE=$(curl -s -o "$TMP/body" -w '%{http_code}' -X "$method" "$base$path" \
            -H 'Content-Type: application/json' -d "$data" ${CURL_EXTRA:-})
    else
        HTTP_CODE=$(curl -s -o "$TMP/body" -w '%{http_code}' -X "$method" "$base$path" ${CURL_EXTRA:-})
    fi
    BODY="$(cat "$TMP/body" 2>/dev/null)"
}

jget() { # jget <python-expr over d> —— 从 $BODY 取字段
    echo "$BODY" | "$PY" -c "import json,sys; d=json.load(sys.stdin); print($1)"
}

ok()   { PASS=$((PASS+1)); echo "PASS  $1"; }
bad()  { FAIL=$((FAIL+1)); echo "FAIL  $1"; }

assert_code() { # assert_code <case> <expected>
    if [ "$HTTP_CODE" = "$2" ]; then ok "$1 (HTTP $2)"; else bad "$1 (expect $2, got $HTTP_CODE): $BODY"; fi
}
assert_eq() { # assert_eq <case> <expected> <actual>
    if [ "$2" = "$3" ]; then ok "$1 ($3)"; else bad "$1 (expect '$2', got '$3')"; fi
}
assert_contains() { # assert_contains <case> <substring>
    case "$BODY" in *"$2"*) ok "$1 (contains $2)";; *) bad "$1 (body missing '$2'): $BODY";; esac
}

wait_ready() { # wait_ready <base> <name>
    local i=0
    while [ $i -lt 60 ]; do
        code=$(curl -s -o /dev/null -w '%{http_code}' "$1/api/instances" 2>/dev/null)
        [ "$code" = "200" ] && return 0
        sleep 0.5; i=$((i+1))
    done
    echo "FATAL: $2 未就绪 ($1)"; exit 1
}

# ── 环境拉起 ───────────────────────────────────────────────────────
[ -f "$MOCK_SCRIPT" ] || { echo "FATAL: mock 不存在: $MOCK_SCRIPT"; exit 1; }

MOCK_PORT=$MOCK_PORT MOCK_LOG="$TMP/mock.log" "$PY" "$MOCK_SCRIPT" >"$TMP/mock.out" 2>&1 &
PIDS+=($!)
sleep 0.3

A2X_REGISTRY_MODE=appliance A2X_REGISTRY_DB_KIND=memory A2X_REGISTRY_PORT=$REG_PORT \
A2X_REGISTRY_YUANRONG_ENDPOINT="http://127.0.0.1:${MOCK_PORT}" \
A2X_REGISTRY_YUANRONG_WAIT_RUNNING_SECONDS=10 \
    "$PY" -m a2x_registry.backend >"$TMP/registry.log" 2>&1 &
PIDS+=($!)

A2X_REGISTRY_MODE=appliance A2X_REGISTRY_DB_KIND=memory A2X_REGISTRY_PORT=$REG_PORT_B \
    "$PY" -m a2x_registry.backend >"$TMP/registry_b.log" 2>&1 &
PIDS+=($!)

wait_ready "$BASE" "registry(main)"
wait_ready "$BASE_B" "registry(未配元戎)"
echo "=== 环境就绪: registry=$BASE mock=:$MOCK_PORT registry_b=$BASE_B ==="

MODE_B_BODY='{
  "name": "user-01+opencode",
  "workspace": "/app",
  "version": "v0.2.0",
  "image_name": "opencode",
  "runtime_spec": {
    "runtime": "python3.11",
    "sandbox_type": "docker",
    "rootfs": {"imageurl": "harbor.local/adapted/opencode:v0.2.0-mod1.3", "user": "agentos"},
    "cpu": 1000, "memory": 2048
  },
  "env_vars": {"A2X_LLM_KEY": "k"},
  "mounts": [{"source": "/data/agent", "target": "/data", "readonly": false}]
}'

# ── C01 模式 A 登记（向前兼容回归）─────────────────────────────────
req "$BASE" POST /api/instances '{
  "service_id": "mode-a-user+aider", "kind": "三方", "framework": "aider",
  "framework_version": "v0.1.0", "node": "192.168.0.12", "address": "10.244.1.7:4096",
  "user": "mode-a-user"}'
assert_code "C01a 模式A登记" 200
assert_eq  "C01b 模式A status" "运行" "$(jget 'd["status"]')"

# ── C02 模式 B 创建（三方）─────────────────────────────────────────
req "$BASE" POST /api/instances "$MODE_B_BODY"
assert_code "C02a 模式B创建" 200
assert_eq "C02b service_id" "user-01+opencode" "$(jget 'd["service_id"]')"
assert_eq "C02c kind" "三方" "$(jget 'd["kind"]')"
assert_eq "C02d framework" "opencode" "$(jget 'd["framework"]')"
assert_eq "C02e framework_version" "v0.2.0" "$(jget 'd["framework_version"]')"
assert_eq "C02f image_name" "opencode" "$(jget 'd.get("image_name","")')"
assert_eq "C02g node 回填" "127.0.0.1" "$(jget 'd["node"]')"
assert_eq "C02h status" "运行" "$(jget 'd["status"]')"
IID="$(jget 'd["instance_id"]')"
if [ -n "$IID" ] && [ "$IID" != "None" ]; then ok "C02i instance_id 回填 ($IID)"; else bad "C02i instance_id 为空"; fi

# ── C03 模式 B 幂等（不二次拉起）───────────────────────────────────
req "$BASE" POST /api/instances "$MODE_B_BODY"
assert_code "C03a 重复创建" 200
assert_eq "C03b instance_id 不变" "$IID" "$(jget 'd["instance_id"]')"

# ── C04 kind 判定（jiuwenswarm → 九问）────────────────────────────
req "$BASE" POST /api/instances '{
  "name": "user-02+jiuwenswarm", "workspace": "/app", "version": "v1.0.0",
  "runtime_spec": {"runtime": "python3.11", "rootfs": {"imageurl": "harbor.local/x:v1"}}}'
assert_code "C04a 九问创建" 200
assert_eq  "C04b kind" "九问" "$(jget 'd["kind"]')"

# ── C05–C09 400 校验 ───────────────────────────────────────────────
req "$BASE" POST /api/instances '{
  "name": "no-plus-user", "workspace": "/app", "version": "v0.2.0",
  "runtime_spec": {"runtime": "python3.11", "rootfs": {"imageurl": "harbor.local/x:v1"}}}'
assert_code "C05 name 无 +" 400

req "$BASE" POST /api/instances '{
  "name": "user-05+opencode", "workspace": "/app",
  "runtime_spec": {"runtime": "python3.11", "rootfs": {"imageurl": "harbor.local/x:v1"}}}'
assert_code "C06 缺 version" 400

req "$BASE" POST /api/instances '{
  "name": "user-05+opencode", "workspace": "app", "version": "v0.2.0",
  "runtime_spec": {"runtime": "python3.11", "rootfs": {"imageurl": "harbor.local/x:v1"}}}'
assert_code "C07 workspace 非绝对路径" 400

req "$BASE" POST /api/instances '{"name": "user-05+opencode", "env_vars": {"A": "b"}}'
assert_code "C08 元戎类字段缺 runtime_spec" 400

req "$BASE" POST /api/instances '{"user": "user-05"}'
assert_code "C09 无法判模式（全缺）" 400

# ── C10 模式 B 优先（同时给 runtime_spec 与 node/address）──────────
req "$BASE" POST /api/instances '{
  "name": "user-03+opencode", "workspace": "/app", "version": "v0.2.0",
  "node": "9.9.9.9", "address": "9.9.9.9:1",
  "runtime_spec": {"runtime": "python3.11", "rootfs": {"imageurl": "harbor.local/x:v1"}}}'
assert_code "C10a B 优先" 200
assert_eq "C10b 落点由元戎覆盖" "127.0.0.1" "$(jget 'd["node"]')"

# ── C11 status 过滤回归 ────────────────────────────────────────────
req "$BASE" PATCH /api/instances/mode-a-user+aider '{"status": "停止"}'
assert_code "C11a PATCH 停止" 200
req "$BASE" GET "/api/instances?framework=aider"
assert_eq "C11b 默认列表不含停止" "0" "$(jget 'len(d)')"
req "$BASE" GET "/api/instances?framework=aider&include_unhealthy=true"
assert_eq "C11c include_unhealthy 可见" "1" "$(jget 'len(d)')"

# ── C12 DELETE 单个 with_runtime=true ─────────────────────────────
req "$BASE" DELETE "/api/instances/user-02+jiuwenswarm?with_runtime=true"
assert_code "C12a 删除(模式B)" 200
assert_eq "C12b deleted" "True" "$(jget 'd["deleted"]')"
assert_eq "C12c runtime_deleted" "True" "$(jget 'd.get("runtime_deleted")')"
req "$BASE" GET "/api/instances?framework=jiuwenswarm&include_unhealthy=true"
assert_eq "C12d 条目已删" "0" "$(jget 'len(d)')"

# ── C13 DELETE 不存在的 id（幂等）──────────────────────────────────
req "$BASE" DELETE "/api/instances/ghost-user+opencode?with_runtime=true"
assert_code "C13a 不存在" 200
assert_eq "C13b deleted=false" "False" "$(jget 'd["deleted"]')"

# ── C14 模式 A 条目（instance_id 空）DELETE with_runtime ───────────
req "$BASE" POST /api/instances '{
  "service_id": "mode-a-2+claude", "kind": "三方", "framework": "claude",
  "framework_version": "v1.0", "node": "192.168.0.12", "address": "10.0.0.2:22",
  "user": "mode-a-2"}'
assert_code "C14a 模式A登记" 200
req "$BASE" DELETE "/api/instances/mode-a-2+claude?with_runtime=true"
assert_code "C14b 删除" 200
assert_eq "C14c runtime_deleted=false" "False" "$(jget 'd.get("runtime_deleted")')"

# ── C15 并发同 service_id（弱断言；强断言在 pytest）────────────────
(curl -s -o "$TMP/c1" -w '%{http_code}' -X POST "$BASE/api/instances" \
    -H 'Content-Type: application/json' -d "${MODE_B_BODY/user-01/user-20}" >"$TMP/c1code") &
P1=$!
(curl -s -o "$TMP/c2" -w '%{http_code}' -X POST "$BASE/api/instances" \
    -H 'Content-Type: application/json' -d "${MODE_B_BODY/user-01/user-20}" >"$TMP/c2code") &
P2=$!
wait $P1 $P2
R1=$(cat "$TMP/c1code"); R2=$(cat "$TMP/c2code")
if [ "$R1" = "200" ] || [ "$R2" = "200" ]; then
    if { [ "$R1" = "200" ] || [ "$R1" = "409" ]; } && { [ "$R2" = "200" ] || [ "$R2" = "409" ]; }; then
        ok "C15 并发 (codes: $R1/$R2)"
    else
        bad "C15 并发非法状态码 ($R1/$R2)"
    fi
else
    bad "C15 并发无成功响应 ($R1/$R2)"
fi

# ── C16 未配元戎 → 501（第二注册中心）─────────────────────────────
req "$BASE_B" POST /api/instances '{
  "name": "user-16+opencode", "workspace": "/app", "version": "v0.2.0",
  "runtime_spec": {"runtime": "python3.11", "rootfs": {"imageurl": "harbor.local/x:v1"}}}'
assert_code "C16a 未配元戎" 501
assert_contains "C16b code=runtime_not_configured" "runtime_not_configured"

# ── C17 在用校验主键关联（image_name 桥接）────────────────────────
req "$BASE" POST /api/images '{
  "name": "oc-pro", "framework": "pro-fw", "version": "v1.0.0",
  "runtime_spec": {"imageurl": "harbor.local/x:v1"}, "uploaded_by": "user-01"}'
assert_code "C17a 注册镜像" 200
req "$BASE" POST /api/instances '{
  "name": "user-10+oc-pro", "workspace": "/app", "version": "v1.0.0", "image_name": "oc-pro",
  "runtime_spec": {"runtime": "python3.11", "rootfs": {"imageurl": "harbor.local/x:v1"}}}'
assert_code "C17b 模式B创建(image_name)" 200
req "$BASE" DELETE /api/images/oc-pro/v1.0.0
assert_code "C17c 镜像在用 409" 409
req "$BASE" DELETE "/api/instances/user-10+oc-pro?with_runtime=true"
assert_code "C17d 删实例" 200
req "$BASE" DELETE /api/images/oc-pro/v1.0.0
assert_code "C17e 实例删后镜像可注销" 200

# ── C18 存量 framework 匹配兼容（无 image_name 的模式 A 条目）─────
req "$BASE" POST /api/images '{
  "name": "leg", "framework": "legacy-abc", "version": "v1.0.0",
  "runtime_spec": {"imageurl": "harbor.local/x:v1"}, "uploaded_by": "user-01"}'
assert_code "C18a 注册镜像" 200
req "$BASE" POST /api/instances '{
  "service_id": "leg-user+legacy-abc", "kind": "三方", "framework": "legacy-abc",
  "framework_version": "v1.0.0", "node": "192.168.0.12", "address": "10.0.0.3:22",
  "user": "leg-user"}'
assert_code "C18b 模式A登记" 200
req "$BASE" DELETE /api/images/leg/v1.0.0
assert_code "C18c framework 匹配在用 409" 409

# ── C19 DELETE ALL with_runtime=true（放最后，清光全部）────────────
req "$BASE" GET "/api/instances?include_unhealthy=true"
TOTAL_BEFORE="$(jget 'len(d)')"
req "$BASE" DELETE "/api/instances/ALL?with_runtime=true"
assert_code "C19a ALL 删除" 200
assert_eq "C19b total" "$TOTAL_BEFORE" "$(jget 'd["total"]')"
assert_eq "C19c deleted=total" "$TOTAL_BEFORE" "$(jget 'd["deleted"]')"
req "$BASE" GET "/api/instances?include_unhealthy=true"
assert_eq "C19d 列表清空" "0" "$(jget 'len(d)')"

# ── 汇总 ──────────────────────────────────────────────────────────
echo "=== 结果: PASS=$PASS FAIL=$FAIL ==="
[ "$FAIL" = "0" ] || { echo "--- registry.log 尾部 ---"; tail -30 "$TMP/registry.log"; exit 1; }
