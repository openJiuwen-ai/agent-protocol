"""Per-session token: bound to auth. Issued only when the receiver has auth
on; an authenticated token grants the session's namespaces, a wrong/absent
token is treated as anonymous (public namespaces only); no-auth clusters use
no token at all.
"""

from __future__ import annotations

from .helpers import FakeRegistry, InProcessTransport, build_store


class _Ctx:
    def __init__(self, is_admin=False, namespaces=None):
        self.is_admin = is_admin
        self.namespaces = namespaces


class _Auth:
    def __init__(self, tokens):
        self._tokens = tokens

    def authenticate(self, token):
        if token in self._tokens:
            return self._tokens[token]
        raise ValueError("bad token")


def _env(dataset, name="x", origin="P", ver=1):
    sid = "generic_" + name
    return {
        "dataset": dataset, "service_id": sid, "origin_id": origin,
        "version": [ver, origin], "tombstone": False,
        "payload": {"entry": {"service_id": sid, "type": "generic", "source": "api_config",
                              "service_data": {"name": name, "description": "d",
                                               "inputSchema": {}, "url": None}},
                    "wrapped": {"id": sid, "type": "generic", "name": name,
                                "description": "d", "metadata": {}}},
    }


def _open_body(node="A"):
    return {"node_id": node, "address": node, "namespaces": [], "token": None}


def test_token_issued_only_when_auth_on(tmp_path):
    t = InProcessTransport()
    open_b = build_store(tmp_path, "Bopen", FakeRegistry(), t, auth_store=None)
    assert open_b.handle_open(_open_body())["session_token"] is None

    auth_b = build_store(tmp_path, "Bauth", FakeRegistry(), t, auth_store=_Auth({}))
    tok = auth_b.handle_open(_open_body())["session_token"]
    assert tok is not None and len(tok) >= 16


def _setup_authed_receiver(tmp_path, t):
    rB = FakeRegistry()
    rB.add_generic("secure", "b-sec")
    rB.add_generic("open", "b-open")
    rB.set_auth_required("secure", True)
    B = build_store(tmp_path, "B", rB, t,
                    auth_store=_Auth({"p": _Ctx(namespaces={"secure"})}))
    # Legit peer P handshakes with a provider token scoped to 'secure'.
    resp = B.handle_open({"node_id": "P", "address": "P",
                          "namespaces": ["secure", "open"], "token": "p"})
    assert set(resp["accepted"]) == {"secure", "open"}
    return B, resp["session_token"]


def test_valid_token_grants_session_wrong_token_public_only(tmp_path):
    t = InProcessTransport()
    B, sess = _setup_authed_receiver(tmp_path, t)

    # Valid token → digest includes the protected namespace.
    rows = B.serve_digest("P", ["secure", "open"], sess)
    assert {r[0] for r in rows} == {"secure", "open"}

    # Wrong token → treated as anonymous → only public 'open'.
    rows_bad = B.serve_digest("P", ["secure", "open"], "WRONG")
    assert {r[0] for r in rows_bad} == {"open"}

    # Absent token → same anonymous downgrade.
    rows_none = B.serve_digest("P", ["secure", "open"], None)
    assert {r[0] for r in rows_none} == {"open"}


def test_spoofed_push_to_protected_namespace_rejected(tmp_path):
    t = InProcessTransport()
    B, sess = _setup_authed_receiver(tmp_path, t)

    # Spoofer claims from_node=P with a wrong token → push to 'secure' rejected.
    res = B.serve_updates("P", [_env("secure", "evil")], "WRONG")
    assert res["accepted"] == 0 and res["rejected"] == 1
    assert all(r["wrapped"]["name"] != "evil" for r in B.foreign_rows("secure"))

    # With the real session token → accepted.
    res = B.serve_updates("P", [_env("secure", "good")], sess)
    assert res["accepted"] == 1
    assert any(r["wrapped"]["name"] == "good" for r in B.foreign_rows("secure"))


def test_keepalive_requires_token_when_auth_on(tmp_path):
    t = InProcessTransport()
    B, sess = _setup_authed_receiver(tmp_path, t)
    assert B.handle_keepalive("P", "WRONG")["ok"] is False
    assert B.handle_keepalive("P", sess)["ok"] is True


def test_session_token_over_http_with_auth(cluster_auth_app):
    """End-to-end over the real HTTP stack (router reads X-Cluster-Session):
    a valid session token exposes the protected namespace in /digest; an
    absent token is downgraded to anonymous (public only)."""
    import uuid

    client, admin = cluster_auth_app
    headers = {"Authorization": f"Bearer {admin}"}

    ds = "secure_" + uuid.uuid4().hex[:6]
    assert client.post("/api/datasets", json={"name": ds, "auth_required": True},
                       headers=headers).status_code == 200
    sid = client.post(f"/api/datasets/{ds}/services/generic",
                      json={"name": "sec-svc", "description": "d"},
                      headers=headers).json()["service_id"]

    # Peer P handshakes with the admin token → session includes the protected ns.
    resp = client.post("/api/cluster/sessions",
                       json={"node_id": "P", "address": "http://p",
                             "namespaces": [ds], "token": admin}).json()
    assert ds in resp["accepted"]
    session_token = resp["session_token"]
    assert session_token  # auth on → a token was issued

    # With the session header → the protected namespace is visible.
    r = client.get("/api/cluster/digest",
                   params={"from_node": "P", "namespaces": ds},
                   headers={"X-Cluster-Session": session_token})
    assert r.status_code == 200
    assert any(row[0] == ds and row[2] == sid for row in r.json())

    # Without the header → anonymous downgrade, protected ns hidden.
    r = client.get("/api/cluster/digest",
                   params={"from_node": "P", "namespaces": ds})
    assert r.status_code == 200
    assert all(row[0] != ds for row in r.json())


def test_no_auth_cluster_ignores_token(tmp_path):
    """End-to-end over the in-process transport: with no auth, sync works
    with no token system at all."""
    t = InProcessTransport()
    rA, rB = FakeRegistry(), FakeRegistry()
    rA.add_generic("open", "a-svc")
    rB.add_generic("open", "b-svc")
    A = build_store(tmp_path, "A", rA, t)
    B = build_store(tmp_path, "B", rB, t)
    A.connect_peer("B")
    # No session token was issued, yet both sides synced.
    assert A._sessions["B"].token is None
    assert any(r["wrapped"]["name"] == "b-svc" for r in A.foreign_rows("open"))
    assert any(r["wrapped"]["name"] == "a-svc" for r in B.foreign_rows("open"))
