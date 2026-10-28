#!/usr/bin/env bash
# Run with: bash scripts/tests/test-build-sdk.sh
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "${SCRIPT_DIR}/../.." && pwd)"
TEST_DIR="$(mktemp -d "${TMPDIR:-/tmp}/build sdk test.XXXXXX")"
trap 'rm -rf -- "${TEST_DIR}"' EXIT

mkdir -p "${TEST_DIR}/scripts"
cp "${ROOT_DIR}/scripts/build-sdk.sh" "${TEST_DIR}/scripts/build-sdk.sh"
ENTRY="${TEST_DIR}/scripts/build-sdk.sh"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

run_entry() {
  local expected_status="$1"
  shift
  local status=0
  output="$(cd / && bash "${ENTRY}" "$@" 2>&1)" || status=$?
  [[ "${status}" -eq "${expected_status}" ]] ||
    fail "expected exit ${expected_status}, got ${status}: ${output}"
}

# CLI validation must work without any project scripts.
run_entry 0 --help
[[ "${output}" == *"Usage:"* ]] || fail "missing help"
run_entry 2
run_entry 2 unknown-sdk

# Missing scripts must fail, including ArkTS's currently absent SDK sources.
for project in A2A/cpp-sdk MCP/cpp-sdk A4P-java-sdk A4P-arkts-sdk; do
  run_entry 1 "${project}"
  [[ "${output}" == *"[ERROR]"*"build script does not exist"* ]] ||
    fail "missing-script diagnostic for ${project}"
done

# Check real script locations too, so moving a component entry breaks this test.
for project in A2A/cpp-sdk MCP/cpp-sdk A4P-java-sdk; do
  [[ -f "${ROOT_DIR}/${project}/scripts/build.sh" ]] ||
    fail "missing repository build script for ${project}"
done

# Fixtures expose the child working directory and each argument separately.
for project in A2A/cpp-sdk MCP/cpp-sdk A4P-java-sdk A4P-arkts-sdk; do
  mkdir -p "${TEST_DIR}/${project}/scripts"
  cat > "${TEST_DIR}/${project}/scripts/build.sh" <<'STUB'
#!/usr/bin/env bash
printf 'cwd=%s\n' "$PWD"
printf 'arg=<%s>\n' "$@"
exit "${BUILD_SDK_TEST_EXIT:-0}"
STUB
  run_entry 0 "${project}" --build-dir "directory with spaces" '*'
  [[ "${output}" == *"cwd=${TEST_DIR}/${project}"* ]] ||
    fail "wrong working directory for ${project}"
  expected_args=$'arg=<--build-dir>\narg=<directory with spaces>\narg=<*>'
  if [[ "${project}" == A4P-java-sdk ]]; then
    expected_args=$'arg=<build>\n'"${expected_args}"
  fi
  [[ "${output}" == *"${expected_args}"* ]] ||
    fail "wrong arguments for ${project}: ${output}"
  [[ "${output}" == *"[OK] ${project}: build completed"* ]] ||
    fail "missing success diagnostic for ${project}"

  export BUILD_SDK_TEST_EXIT=17
  run_entry 17 "${project}"
  unset BUILD_SDK_TEST_EXIT
  [[ "${output}" == *"[ERROR] ${project}: build failed (exit code 17)"* ]] ||
    fail "missing failure diagnostic for ${project}"
done

echo "PASS: build entry validation, dispatch, arguments, and exit status"
