#!/usr/bin/env bash
# rqlite 3 节点集群一键部署脚本（供 pytest 启动时集成）
#
# 用法：
#   ./start_rqlite_cluster.sh [--image-tar PATH] [up|down|status|clean|restart]
#   ./start_rqlite_cluster.sh --help
#
# 默认动作：up
#
# pytest 集成示例（conftest.py session fixture 里）：
#   ret = subprocess.run(["bash", SCRIPT, "up"]).returncode
#   if ret != 0: pytest.skip("rqlite cluster unavailable")
#
# 环境变量（可选）：
#   RQLITE_IMAGE      镜像名:tag（默认 rqlite/rqlite:latest）
#   BOOTSTRAP_TIMEOUT 选主等待秒数（默认 60）

set -euo pipefail

# ── 常量 ────────────────────────────────────────────────────
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
COMPOSE_FILE="$SCRIPT_DIR/compose.yaml"
DEFAULT_IMAGE="rqlite/rqlite:latest"
IMAGE_NAME="${RQLITE_IMAGE:-$DEFAULT_IMAGE}"
BOOTSTRAP_TIMEOUT="${BOOTSTRAP_TIMEOUT:-60}"
# 宿主暴露的 3 个 HTTP 端口（与 compose.yaml 映射一致）
HTTP_PORTS=(4001 4011 4021)
PROJECT_NAME="rqliteAutomaticClustering"

# ── 参数解析 ────────────────────────────────────────────────
ACTION="up"
IMAGE_TAR=""

usage() {
  cat <<'EOF'
Usage: start_rqlite_cluster.sh [--image-tar PATH] [up|down|status|clean|restart]

Actions:
  up        启动集群并等待 leader 就绪（默认；幂等）
  down      停止集群（保留卷，数据可复用）
  clean     停止并删除卷（彻底清数据）
  restart   down + up
  status    查看集群状态（容器 + leader + 节点）

Options:
  --image-tar PATH   本地 rqlite 镜像 tar 包路径；镜像缺失时优先加载
  -h, --help         显示本帮助

Env:
  RQLITE_IMAGE       镜像名:tag（默认 rqlite/rqlite:latest）
  BOOTSTRAP_TIMEOUT  选主等待秒数（默认 60）
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --image-tar)
      [[ $# -lt 2 ]] && { echo "Error: --image-tar 需要参数" >&2; exit 2; }
      IMAGE_TAR="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    up|down|status|clean|restart) ACTION="$1"; shift ;;
    *) echo "Error: 未知参数 '$1'" >&2; usage >&2; exit 2 ;;
  esac
done

# ── 工具函数 ────────────────────────────────────────────────
log()  { echo "[rqlite] $*"; }
err()  { echo "[rqlite][ERROR] $*" >&2; }

# 写出 compose.yaml（脚本为唯一真源；每次 up 重新生成以保持同步）
write_compose_file() {
  # 把 IMAGE_NAME 注入到 image: 字段；用 sed 替换占位符
  cat > "$COMPOSE_FILE" <<YAML
# 由 start_rqlite_cluster.sh 自动生成 —— 请勿直接编辑。
# 修改请改 start_rqlite_cluster.sh 中的 compose 模板。
# 镜像：${IMAGE_NAME}

name: ${PROJECT_NAME}

services:

  myrqlite-service-1:
    image: ${IMAGE_NAME}
    container_name: myrqlite-container-1
    hostname: myrqlite-host-1
    volumes:
      - rqlite-data-node-1:/rqlite/file
    ports:
      - "4001:4001"
      - "4002:4002"
    command: -bootstrap-expect 3 -join myrqlite-service-1:4002,myrqlite-service-2:4002,myrqlite-service-3:4002
    environment:
      NODE_ID: myrqlite-node-1
      HTTP_ADDR: myrqlite-host-1:4001
      RAFT_ADDR: myrqlite-host-1:4002
      SQLITE_EXTENSIONS: "sqlean,sqlite-vec,misc"

  myrqlite-service-2:
    image: ${IMAGE_NAME}
    container_name: myrqlite-container-2
    hostname: myrqlite-host-2
    volumes:
      - rqlite-data-node-2:/rqlite/file
    ports:
      - "4011:4001"
      - "4012:4002"
    command: -bootstrap-expect 3 -join myrqlite-service-1:4002,myrqlite-service-2:4002,myrqlite-service-3:4002
    environment:
      NODE_ID: myrqlite-node-2
      HTTP_ADDR: myrqlite-host-2:4001
      RAFT_ADDR: myrqlite-host-2:4002
      SQLITE_EXTENSIONS: "sqlean,sqlite-vec,misc"

  myrqlite-service-3:
    image: ${IMAGE_NAME}
    container_name: myrqlite-container-3
    hostname: myrqlite-host-3
    volumes:
      - rqlite-data-node-3:/rqlite/file
    ports:
      - "4021:4001"
      - "4022:4002"
    command: -bootstrap-expect 3 -join myrqlite-service-1:4002,myrqlite-service-2:4002,myrqlite-service-3:4002
    environment:
      NODE_ID: myrqlite-node-3
      HTTP_ADDR: myrqlite-host-3:4001
      RAFT_ADDR: myrqlite-host-3:4002
      SQLITE_EXTENSIONS: "sqlean,sqlite-vec,misc"

volumes:
    rqlite-data-node-1: {}
    rqlite-data-node-2: {}
    rqlite-data-node-3: {}
YAML
}

# 确保镜像可用：本地已加载 > tar 包 > docker pull
ensure_image() {
  if docker image inspect "$IMAGE_NAME" >/dev/null 2>&1; then
    log "镜像已存在：$IMAGE_NAME"
    return 0
  fi
  # 镜像缺失 —— 优先用 tar 包
  if [[ -n "$IMAGE_TAR" ]]; then
    if [[ -f "$IMAGE_TAR" ]]; then
      log "从 tar 包加载镜像：$IMAGE_TAR"
      docker load -i "$IMAGE_TAR"
      if docker image inspect "$IMAGE_NAME" >/dev/null 2>&1; then
        log "tar 包加载成功"
        return 0
      fi
      err "tar 包加载完成，但未找到镜像 $IMAGE_NAME（tar 内 tag 可能不同，请检查）"
      # 落到 pull 兜底
    else
      err "指定的 tar 包不存在：$IMAGE_TAR"
      # 落到 pull 兜底
    fi
  fi
  # 兜底：尝试在线拉取（离线环境会失败）
  log "尝试 docker pull $IMAGE_NAME（需要公网）"
  if docker pull "$IMAGE_NAME"; then
    log "镜像拉取成功"
    return 0
  fi
  err "无法获取镜像 $IMAGE_NAME（tar 包未提供/无效且 docker pull 失败）"
  return 1
}

# compose 包装（不传 -p：compose 会用 compose.yaml 里的 name: 字段，
# 该字段会被 docker 自动小写化为 rqliteautomaticclustering，与现有集群一致；
# 显式 -p 对大写字母校验更严，会拒绝 rqliteAutomaticClustering）
dc() {
  docker compose -f "$COMPOSE_FILE" "$@";
}

# 判断集群是否已运行（至少一个容器 running）
cluster_running() {
  dc ps --services --filter "status=running" 2>/dev/null | grep -q . || return 1
}

# 探测某端口是否为 leader，是则输出端点 URL，否则空
leader_at_port() {
  local port="$1"
  curl -fsS --max-time 3 "http://127.0.0.1:$port/status" 2>/dev/null \
    | python3 -c '
import sys, json
try:
    s = json.load(sys.stdin).get("store", {})
    l = s.get("leader")
    a = s.get("addr", "")
    lr = l.get("addr", "") if isinstance(l, dict) else (l or "")
    if lr and a and lr == a:
        print("LEADER")
except Exception:
    pass
' 2>/dev/null
}

# 等待任一节点选出 leader，输出 leader 的 HTTP 端点
wait_for_leader() {
  local deadline=$((SECONDS + BOOTSTRAP_TIMEOUT))
  local port
  while (( SECONDS < deadline )); do
    for port in "${HTTP_PORTS[@]}"; do
      if [[ "$(leader_at_port "$port")" == "LEADER" ]]; then
        echo "http://127.0.0.1:$port"
        return 0
      fi
    done
    sleep 1
  done
  return 1
}

# 校验集群就绪（3 容器 running + leader 已选出 + 3 节点可见）
verify_ready() {
  if ! cluster_running; then
    err "集群未运行"
    return 1
  fi
  local leader
  leader="$(wait_for_leader)" || { err "等待 leader 超时（${BOOTSTRAP_TIMEOUT}s）"; return 1; }
  # 校验 /nodes 返回 3 个 voter
  local port
  port="${leader##*:}"
  local n
  n="$(curl -fsS --max-time 3 "http://127.0.0.1:$port/nodes" 2>/dev/null \
       | python3 -c 'import sys,json; print(len(json.load(sys.stdin)))' 2>/dev/null || echo 0)"
  if (( n < 3 )); then
    err "集群节点数不足：$n（期望 3）"
    return 1
  fi
  echo "$leader"
  return 0
}

# ── 动作 ────────────────────────────────────────────────────
do_up() {
  ensure_image || return 1
  write_compose_file
  if cluster_running; then
    log "集群已在运行，校验就绪状态"
  else
    log "启动 3 节点集群"
    dc up -d
  fi
  log "等待 leader 选举完成（最长 ${BOOTSTRAP_TIMEOUT}s）"
  local leader
  leader="$(wait_for_leader)" || { err "无 leader 选出"; return 1; }
  log "leader：$leader"
  # 校验节点数
  local port="${leader##*:}"
  local n
  n="$(curl -fsS --max-time 3 "http://127.0.0.1:$port/nodes" 2>/dev/null \
       | python3 -c 'import sys,json; print(len(json.load(sys.stdin)))' 2>/dev/null || echo 0)"
  log "集群节点数：$n"
  if (( n < 3 )); then
    err "节点数不足，集群未完全组建"
    return 1
  fi
  log "集群就绪：$leader"
  echo "$leader"
  return 0
}

do_down() {
  log "停止集群（保留卷）"
  dc down
}

do_clean() {
  log "停止集群并删除卷（彻底清数据）"
  dc down -v
}

do_restart() {
  do_down || true
  do_up
}

do_status() {
  log "compose ps："
  dc ps || true
  echo
  for port in "${HTTP_PORTS[@]}"; do
    local addr
    addr="$(curl -fsS --max-time 3 "http://127.0.0.1:$port/status" 2>/dev/null \
            | python3 -c '
import sys,json
try:
    s=json.load(sys.stdin).get("store",{})
    l=s.get("leader")
    lr=l.get("addr","") if isinstance(l,dict) else (l or "")
    print("addr=" + str(s.get("addr","")) + " leader=" + str(lr) + " ready=" + str(s.get("ready")))
except Exception:
    print("UNREACHABLE")
' 2>/dev/null || echo "UNREACHABLE")"
    log "node 127.0.0.1:$port -> $addr"
  done
  echo
  local leader
  leader="$(wait_for_leader)" && log "当前 leader：$leader" || err "无 leader"
}

# ── 主流程 ──────────────────────────────────────────────────
case "$ACTION" in
  up)      do_up ;;
  down)    do_down ;;
  clean)   do_clean ;;
  restart) do_restart ;;
  status)  do_status ;;
  *)       err "未知动作：$ACTION"; usage >&2; exit 2 ;;
esac
