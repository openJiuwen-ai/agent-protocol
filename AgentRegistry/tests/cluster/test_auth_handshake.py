"""Per-namespace authorization for the handshake (reuses auth semantics)."""

from __future__ import annotations

from a2x_registry.cluster.auth_handshake import authorize_namespaces


class _Ctx:
    def __init__(self, is_admin=False, namespaces=None):
        self.is_admin = is_admin
        self.namespaces = namespaces  # None=all (admin) or set


class _AuthStore:
    def __init__(self, tokens):
        self._tokens = tokens  # token -> ctx

    def authenticate(self, token):
        if token in self._tokens:
            return self._tokens[token]
        raise ValueError("bad token")


class _Registry:
    def __init__(self, datasets, auth_required=()):
        self._ds = list(datasets)
        self._auth = set(auth_required)

    def list_datasets(self):
        return list(self._ds)

    def is_auth_required(self, ds):
        return ds in self._auth


def test_no_auth_store_accepts_everything():
    reg = _Registry(["have"])
    accepted, ephemeral = authorize_namespaces(reg, None, ["have", "new"], None)
    assert set(accepted) == {"have", "new"}
    assert ephemeral == ["new"]  # 'new' doesn't exist locally → ephemeral


def test_existing_anon_namespace_accepted_without_token():
    reg = _Registry(["have"], auth_required=())
    store = _AuthStore({})
    accepted, ephemeral = authorize_namespaces(reg, store, ["have"], None)
    assert accepted == ["have"]
    assert ephemeral == []


def test_existing_authreq_namespace_needs_provider():
    reg = _Registry(["secure"], auth_required=["secure"])
    ctx = _Ctx(is_admin=False, namespaces={"secure"})
    store = _AuthStore({"tok": ctx})

    # With a scoped provider token → accepted.
    accepted, _ = authorize_namespaces(reg, store, ["secure"], "tok")
    assert accepted == ["secure"]

    # Without a token → rejected.
    accepted, _ = authorize_namespaces(reg, store, ["secure"], None)
    assert accepted == []

    # Provider scoped to a different namespace → rejected.
    ctx2 = _Ctx(is_admin=False, namespaces={"other"})
    store2 = _AuthStore({"tok": ctx2})
    accepted, _ = authorize_namespaces(reg, store2, ["secure"], "tok")
    assert accepted == []


def test_missing_namespace_needs_admin():
    reg = _Registry(["have"], auth_required=())
    admin = _AuthStore({"adm": _Ctx(is_admin=True, namespaces=None)})
    prov = _AuthStore({"p": _Ctx(is_admin=False, namespaces={"have"})})

    # Admin token → ephemeral namespace created.
    accepted, ephemeral = authorize_namespaces(reg, admin, ["new"], "adm")
    assert accepted == ["new"] and ephemeral == ["new"]

    # Non-admin token → rejected.
    accepted, ephemeral = authorize_namespaces(reg, prov, ["new"], "p")
    assert accepted == [] and ephemeral == []


def test_admin_accepts_authrequired_existing():
    reg = _Registry(["secure"], auth_required=["secure"])
    admin = _AuthStore({"adm": _Ctx(is_admin=True, namespaces=None)})
    accepted, _ = authorize_namespaces(reg, admin, ["secure"], "adm")
    assert accepted == ["secure"]
