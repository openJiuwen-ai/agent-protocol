"""Real two-process end-to-end smoke over HTTP (exercises HttpTransport).

Spins up two actual `a2x-registry` servers on separate ports, each with its
own home + cluster_state.json (node ids A / B), connects them via the real
`POST /api/cluster/peers` trigger, and verifies bidirectional sync through
the public dataset endpoints.

Skips (not fails) if the servers can't be started in this environment, so a
sandbox without subprocess/network access doesn't break the suite.
"""

from __future__ import annotations

import os
import socket
import subprocess
import sys
import time
from pathlib import Path

import pytest

httpx = pytest.importorskip("httpx")

REPO_ROOT = Path(__file__).resolve().parents[2]
STARTUP_TIMEOUT = 60.0   # warmup can take a while on a cold import
SYNC_TIMEOUT = 15.0


def _free_port() -> int:
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


def _node_env(home: Path, port: int) -> dict:
    env = os.environ.copy()
    env.update({
        "A2X_REGISTRY_HOME": str(home),
        "A2X_REGISTRY_CLUSTER_STATE": str(home / "cluster_state.json"),
        "A2X_REGISTRY_CLUSTER_ADVERTISE": f"http://127.0.0.1:{port}",
        "A2X_REGISTRY_AUTH_DATA": str(home / "auth_data"),
        "NO_PROXY": "127.0.0.1,localhost",
        "PYTHONUTF8": "1",
    })
    return env


def _start_node(name: str, home: Path, port: int):
    home.mkdir(parents=True, exist_ok=True)
    env = _node_env(home, port)
    # Offline: generate the node identity (writes cluster_state.json).
    init = subprocess.run(
        [sys.executable, "-m", "a2x_registry.backend", "cluster", "init", "--node-id", name],
        env=env, cwd=str(REPO_ROOT), capture_output=True, text=True,
    )
    if init.returncode != 0:
        pytest.skip(f"cluster init failed for {name}: {init.stderr or init.stdout}")
    log = open(home / "server.log", "w", encoding="utf-8")
    proc = subprocess.Popen(
        [sys.executable, "-m", "a2x_registry.backend", "--host", "127.0.0.1", "--port", str(port)],
        env=env, cwd=str(REPO_ROOT), stdout=log, stderr=subprocess.STDOUT,
    )
    return proc, log


def _wait_ready(client, base: str) -> bool:
    """Cluster module loads last in warmup, so a 200 on /state means the
    registry + cluster are both up."""
    deadline = time.time() + STARTUP_TIMEOUT
    while time.time() < deadline:
        try:
            r = client.get(f"{base}/api/cluster/state", timeout=2.0)
            if r.status_code == 200:
                return True
        except httpx.HTTPError:
            pass
        time.sleep(0.5)
    return False


def _eventually(fn, timeout=SYNC_TIMEOUT):
    deadline = time.time() + timeout
    last = None
    while time.time() < deadline:
        last = fn()
        if last:
            return last
        time.sleep(0.3)
    return last


def test_two_process_http_sync(tmp_path):
    pa, pb = _free_port(), _free_port()
    base_a, base_b = f"http://127.0.0.1:{pa}", f"http://127.0.0.1:{pb}"
    procs = []
    logs = []
    client = httpx.Client(trust_env=False, timeout=10.0)
    try:
        for name, port in (("A", pa), ("B", pb)):
            proc, log = _start_node(name, tmp_path / name, port)
            procs.append(proc)
            logs.append(log)

        if not (_wait_ready(client, base_a) and _wait_ready(client, base_b)):
            pytest.skip("servers did not become ready (no subprocess/network in sandbox?)")

        # Each side hosts a dataset + one service.
        for base, svc in ((base_a, "a-svc"), (base_b, "b-svc")):
            assert client.post(f"{base}/api/datasets", json={"name": "svc"}).status_code == 200
            r = client.post(f"{base}/api/datasets/svc/services/generic",
                            json={"name": svc, "description": "d"})
            assert r.status_code == 200, r.text

        # Trigger: A connects to B (real HttpTransport A→B handshake+reconcile).
        r = client.post(f"{base_a}/api/cluster/peers", json={"address": base_b})
        assert r.status_code == 200, r.text
        assert r.json()["peer"]["node_id"] == "B"

        # A sees B's service (namespaced id, origin B); B sees A's.
        def a_sees_b():
            rows = client.get(f"{base_a}/api/datasets/svc/services").json()
            return any(x.get("origin_id") == "B" and x["name"] == "b-svc" for x in rows)

        def b_sees_a():
            rows = client.get(f"{base_b}/api/datasets/svc/services").json()
            return any(x.get("origin_id") == "A" and x["name"] == "a-svc" for x in rows)

        assert _eventually(a_sees_b), "A did not receive B's service over HTTP"
        assert _eventually(b_sees_a), "B did not receive A's service over HTTP"

        # A new registration on A propagates to B incrementally (push).
        assert client.post(f"{base_a}/api/datasets/svc/services/generic",
                           json={"name": "a-extra", "description": "d"}).status_code == 200

        def b_sees_extra():
            rows = client.get(f"{base_b}/api/datasets/svc/services").json()
            return any(x.get("origin_id") == "A" and x["name"] == "a-extra" for x in rows)

        assert _eventually(b_sees_extra), "incremental push did not reach B over HTTP"
    finally:
        client.close()
        for proc in procs:
            proc.terminate()
            try:
                proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                proc.kill()
        for log in logs:
            log.close()


def test_three_process_membership_set(tmp_path):
    """`cluster set add` over real HTTP: A pulls B and C into one cluster
    (auto full-mesh), a service on C reaches A, then `set remove` drops B
    everywhere."""
    ports = {n: _free_port() for n in ("A", "B", "C")}
    bases = {n: f"http://127.0.0.1:{p}" for n, p in ports.items()}
    procs, logs = [], []
    client = httpx.Client(trust_env=False, timeout=10.0)
    try:
        for name in ("A", "B", "C"):
            proc, log = _start_node(name, tmp_path / name, ports[name])
            procs.append(proc)
            logs.append(log)
        for name in ("A", "B", "C"):
            if not _wait_ready(client, bases[name]):
                pytest.skip("servers did not become ready (no subprocess/network in sandbox?)")

        # Same dataset on every node so the namespace is negotiated.
        for name in ("A", "B", "C"):
            assert client.post(f"{bases[name]}/api/datasets", json={"name": "svc"}).status_code == 200
        assert client.post(f"{bases['C']}/api/datasets/svc/services/generic",
                           json={"name": "c-svc", "description": "d"}).status_code == 200

        # A declaratively forms the cluster with B and C.
        r = client.post(f"{bases['A']}/api/cluster/set/add",
                        json={"members": [{"address": bases["B"]}, {"address": bases["C"]}]})
        assert r.status_code == 200, r.text
        assert r.json()["cluster_id"].startswith("clu-")

        # Roster converges to all three on A.
        def a_roster():
            ids = {m["node_id"] for m in client.get(f"{bases['A']}/api/cluster/set").json()["roster"]}
            return ids == {"A", "B", "C"}
        assert _eventually(a_roster), "A's roster did not converge to {A,B,C}"

        # C's service reaches A (full mesh, direct broadcast).
        def a_sees_c():
            rows = client.get(f"{bases['A']}/api/datasets/svc/services").json()
            return any(x.get("origin_id") == "C" and x["name"] == "c-svc" for x in rows)
        assert _eventually(a_sees_c), "A did not receive C's service"

        # Remove B → it disappears from A's roster and reverts to standalone.
        assert client.post(f"{bases['A']}/api/cluster/set/remove",
                           json={"members": [{"node_id": "B"}]}).status_code == 200

        def b_gone_from_a():
            ids = {m["node_id"] for m in client.get(f"{bases['A']}/api/cluster/set").json()["roster"]}
            return "B" not in ids
        assert _eventually(b_gone_from_a), "B not removed from A's roster"

        def b_standalone():
            return client.get(f"{bases['B']}/api/cluster/set").json()["cluster_id"] is None
        assert _eventually(b_standalone), "B did not revert to standalone"
    finally:
        client.close()
        for proc in procs:
            proc.terminate()
            try:
                proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                proc.kill()
        for log in logs:
            log.close()
