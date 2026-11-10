"""All dataset JSON writers preserve the last state on replacement failure."""

import pytest

from a2x_registry.common import atomic
from a2x_registry.register.store import RegistryStore


@pytest.mark.parametrize("filename,method,args", [
    ("api_config.json", "save_api_batch", ([],)),
    ("service.json", "write_service_json", ([],)),
    ("register_config.json", "write_register_config", ({"a2a": "1.0"},)),
    ("vector_config.json", "write_vector_config", ("model", 128)),
    ("auth_config.json", "write_auth_config", (True,)),
    ("lease_config.json", "write_lease_config", (True, 10, 60, 30)),
])
def test_dataset_writer_propagates_failure_and_preserves_old_json(
    tmp_path, monkeypatch, filename, method, args,
):
    store = RegistryStore(tmp_path)
    path = tmp_path / filename
    original = b'{"previous": true}\n'
    path.write_bytes(original)

    def deny_replace(source, target):
        raise PermissionError("destination locked")

    monkeypatch.setattr(atomic.os, "replace", deny_replace)
    monkeypatch.setattr(atomic.time, "sleep", lambda _: None)
    with pytest.raises(PermissionError, match="destination locked"):
        getattr(store, method)(*args)
    assert path.read_bytes() == original
    assert set(tmp_path.iterdir()) == {path}
