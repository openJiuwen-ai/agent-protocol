"""Transport's 400 dispatch — heartbeat code → typed subclass."""

from __future__ import annotations

import httpx
import pytest

from a2x_registry_client.errors import (
    A2XHeartbeatNotSupportedError,
    A2XTTLOutOfRangeError,
    A2XTTLRequiredError,
    ValidationError,
)
from a2x_registry_client.transport import HTTPTransport


def _transport_with_handler(handler):
    mock = httpx.MockTransport(handler)
    t = HTTPTransport(base_url="http://test/")
    t._client.close()
    t._client = httpx.Client(base_url="http://test/", transport=mock)
    return t


def test_400_ttl_required_dispatches_to_subclass():
    body = {
        "detail": {
            "code": "ttl_required",
            "detail": "Namespace 'x' requires lease_ttl",
            "min_ttl": 10,
            "max_ttl": 60,
        }
    }
    t = _transport_with_handler(lambda req: httpx.Response(400, json=body))
    with pytest.raises(A2XTTLRequiredError) as exc:
        t.request("POST", "/whatever")
    assert exc.value.min_ttl == 10
    assert exc.value.max_ttl == 60
    # Also inherits ValidationError so existing catches still work.
    assert isinstance(exc.value, ValidationError)


def test_400_ttl_out_of_range_dispatches_to_subclass():
    body = {"detail": {"code": "ttl_out_of_range", "detail": "...", "min_ttl": 5, "max_ttl": 60}}
    t = _transport_with_handler(lambda req: httpx.Response(400, json=body))
    with pytest.raises(A2XTTLOutOfRangeError) as exc:
        t.request("POST", "/whatever")
    assert exc.value.min_ttl == 5
    assert exc.value.max_ttl == 60


def test_400_not_supported_dispatches_to_subclass():
    body = {"detail": {"code": "heartbeat_not_supported", "detail": "Namespace doesn't enable heartbeat"}}
    t = _transport_with_handler(lambda req: httpx.Response(400, json=body))
    with pytest.raises(A2XHeartbeatNotSupportedError):
        t.request("POST", "/whatever")


def test_400_unknown_code_falls_through_to_validation_error():
    body = {"detail": {"code": "made_up_code", "detail": "..."}}
    t = _transport_with_handler(lambda req: httpx.Response(400, json=body))
    with pytest.raises(ValidationError) as exc:
        t.request("POST", "/whatever")
    # NOT the heartbeat subclasses
    assert not isinstance(exc.value, A2XTTLRequiredError)
    assert not isinstance(exc.value, A2XTTLOutOfRangeError)
    assert not isinstance(exc.value, A2XHeartbeatNotSupportedError)


def test_400_legacy_string_detail_still_works():
    """Server emitting plain string detail (legacy) still maps to ValidationError."""
    body = {"detail": "boring 400 message"}
    t = _transport_with_handler(lambda req: httpx.Response(400, json=body))
    with pytest.raises(ValidationError):
        t.request("POST", "/whatever")
