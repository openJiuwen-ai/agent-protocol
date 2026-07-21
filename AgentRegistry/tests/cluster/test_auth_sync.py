"""End-to-end sync under various auth states.

Handshake authorization gates which namespaces a session syncs; everything
downstream (digest/pull/push) is scoped to that accepted set. These
tests drive real connect+reconcile to confirm the gating end-to-end — with
special attention to the no-auth-anywhere case.
"""

from __future__ import annotations

from .helpers import FakeRegistry, InProcessTransport, build_store, converge, visible


# ── fake auth ────────────────────────────────────────────────────────────

class _Ctx:
    def __init__(self, is_admin=False, namespaces=None):
        self.is_admin = is_admin
        self.namespaces = namespaces  # None=all (admin) or set


class _Auth:
    def __init__(self, tokens):
        self._tokens = tokens

    def authenticate(self, token):
        if token in self._tokens:
            return self._tokens[token]
        raise ValueError("bad token")


def _foreign_names(store, dataset):
    return {r["wrapped"]["name"] for r in store.foreign_rows(dataset)}


# ── the emphasized case: no auth anywhere ────────────────────────────────

def test_no_auth_everywhere_full_sync(tmp_path):
    """When no registry enables auth, every namespace syncs with no token."""
    t = InProcessTransport()
    rA, rB = FakeRegistry(), FakeRegistry()
    rA.add_generic("open", "a-svc")
    rB.add_generic("open", "b-svc")
    rB.add_generic("other", "x-svc")            # B-only namespace
    A = build_store(tmp_path, "A", rA, t)        # auth_store defaults to None
    B = build_store(tmp_path, "B", rB, t)

    A.connect_peer("B")                          # NO token
    converge([A, B])

    # Both directions, both namespaces (B's 'other' is ephemeral on A).
    assert visible(A, rA, "open") == {"a-svc", "b-svc"}
    assert visible(B, rB, "open") == {"a-svc", "b-svc"}
    assert _foreign_names(A, "other") == {"x-svc"}


def test_no_auth_three_node_chain_full_sync(tmp_path):
    """No-auth chain A-B-C still reaches full any-to-any visibility."""
    t = InProcessTransport()
    regs = {n: FakeRegistry() for n in "ABC"}
    for n in "ABC":
        regs[n].add_generic("open", f"{n}-svc")
    A = build_store(tmp_path, "A", regs["A"], t)
    B = build_store(tmp_path, "B", regs["B"], t)
    C = build_store(tmp_path, "C", regs["C"], t)
    B.connect_peer("A")
    B.connect_peer("C")
    converge([A, B, C])

    everyone = {"A-svc", "B-svc", "C-svc"}
    assert visible(A, regs["A"], "open") == everyone
    assert visible(C, regs["C"], "open") == everyone


# ── auth-required namespaces ─────────────────────────────────────────────

def test_authrequired_blocked_without_token_anon_still_syncs(tmp_path):
    t = InProcessTransport()
    rA, rB = FakeRegistry(), FakeRegistry()
    rA.add_generic("secure", "a-sec")
    rA.add_generic("open", "a-open")
    rB.add_generic("secure", "b-sec")
    rB.add_generic("open", "b-open")
    rB.set_auth_required("secure", True)
    B_auth = _Auth({"p": _Ctx(namespaces={"secure"})})
    A = build_store(tmp_path, "A", rA, t)
    B = build_store(tmp_path, "B", rB, t, auth_store=B_auth)

    A.connect_peer("B")                          # no token
    converge([A, B])

    # 'open' (anon) syncs both ways; 'secure' (auth-required) is blocked.
    assert _foreign_names(B, "open") == {"a-open"}
    assert _foreign_names(A, "open") == {"b-open"}
    assert _foreign_names(B, "secure") == set()
    assert _foreign_names(A, "secure") == set()


def test_authrequired_syncs_with_provider_token(tmp_path):
    t = InProcessTransport()
    rA, rB = FakeRegistry(), FakeRegistry()
    rA.add_generic("secure", "a-sec")
    rB.add_generic("secure", "b-sec")
    rB.set_auth_required("secure", True)
    B_auth = _Auth({"p": _Ctx(namespaces={"secure"})})
    A = build_store(tmp_path, "A", rA, t)
    B = build_store(tmp_path, "B", rB, t, auth_store=B_auth)

    A.connect_peer("B", token="p")               # scoped provider token
    converge([A, B])

    assert _foreign_names(B, "secure") == {"a-sec"}
    assert _foreign_names(A, "secure") == {"b-sec"}


def test_anon_namespace_syncs_on_auth_enabled_registry(tmp_path):
    """A registry that HAS auth initialized still syncs its non-auth-required
    namespaces without any token."""
    t = InProcessTransport()
    rA, rB = FakeRegistry(), FakeRegistry()
    rA.add_generic("open", "a-open")
    rB.add_generic("open", "b-open")
    # B has an auth store but 'open' is not auth_required.
    B_auth = _Auth({"adm": _Ctx(is_admin=True)})
    A = build_store(tmp_path, "A", rA, t)
    B = build_store(tmp_path, "B", rB, t, auth_store=B_auth)

    A.connect_peer("B")                          # no token
    converge([A, B])
    assert _foreign_names(A, "open") == {"b-open"}
    assert _foreign_names(B, "open") == {"a-open"}


def _env(dataset, name="x", origin="X", ver=1):
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


def test_updates_push_rejected_for_protected_namespace_without_session(tmp_path):
    """Direct /updates can't bypass the handshake: a record for an
    auth_required namespace from a peer with no session is rejected, while a
    public namespace is accepted."""
    t = InProcessTransport()
    rB = FakeRegistry()
    rB.add_generic("secure", "b-sec")
    rB.set_auth_required("secure", True)
    rB.add_generic("open", "b-open")
    B = build_store(tmp_path, "B", rB, t, auth_store=_Auth({"p": _Ctx(namespaces={"secure"})}))

    res = B.serve_updates("X", [_env("secure", "evil")])      # no session for X
    assert res["accepted"] == 0 and res["rejected"] == 1
    assert _foreign_names(B, "secure") == set()

    res = B.serve_updates("X", [_env("open", "ok")])          # public ns
    assert res["accepted"] == 1
    assert "ok" in _foreign_names(B, "open")


def test_updates_push_open_when_no_auth_anywhere(tmp_path):
    """With no auth configured, /updates accepts any namespace (open cluster),
    including one the receiver doesn't have locally."""
    t = InProcessTransport()
    B = build_store(tmp_path, "B", FakeRegistry(), t, auth_store=None)
    res = B.serve_updates("X", [_env("brand_new_ns", "y")])
    assert res["accepted"] == 1
    assert "y" in _foreign_names(B, "brand_new_ns")


def test_admin_token_creates_ephemeral_namespace(tmp_path):
    """A namespace the receiver doesn't have requires an admin token; the
    receiver then hosts the replicas (ephemeral)."""
    t = InProcessTransport()
    rA, rB = FakeRegistry(), FakeRegistry()
    rA.add_generic("newns", "a-only")            # B doesn't have 'newns'
    rB.add_generic("open", "b-open")
    B_auth = _Auth({"adm": _Ctx(is_admin=True)})
    A = build_store(tmp_path, "A", rA, t, auth_store=None)
    B = build_store(tmp_path, "B", rB, t, auth_store=B_auth)

    # Without admin token → B refuses to host 'newns'.
    A.connect_peer("B")
    converge([A, B])
    assert _foreign_names(B, "newns") == set()

    # With admin token → B hosts the ephemeral replica.
    A.connect_peer("B", token="adm")
    converge([A, B])
    assert _foreign_names(B, "newns") == {"a-only"}
