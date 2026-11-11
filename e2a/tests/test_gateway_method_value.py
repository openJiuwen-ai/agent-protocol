# Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.

"""Method normalization preserves names and rejects invalid values."""

from types import SimpleNamespace

import pytest

from e2a.gateway_normalize import (
    E2A_INTERNAL_CONTEXT_KEY,
    E2A_LEGACY_AGENT_REQUEST_KEY,
    e2a_from_agent_fields,
    message_to_e2a,
    message_to_e2a_or_fallback,
    message_to_legacy_agent_dict,
    method_value,
)
from enum import Enum

from e2a.message import Message


class _Method(Enum):
    """Stand-in for the implementation-side method enum."""

    CHAT_SEND = "chat.send"


def _message(method):
    return Message(
        id="method-test",
        type="req",
        channel_id="web",
        session_id=None,
        params={},
        timestamp=1.0,
        ok=True,
        req_method=method,
    )


@pytest.mark.parametrize(
    "method,expected",
    [(_Method.CHAT_SEND, "chat.send"), ("future.extension", "future.extension"),
     ("0", "0"), ("False", "False")],
)
def test_valid_method_names_are_preserved(method, expected):
    assert method_value(method) == expected
    assert message_to_e2a(_message(method)).method == expected
    assert message_to_legacy_agent_dict(_message(method))["req_method"] == expected
    assert e2a_from_agent_fields(request_id="method-test", req_method=method).method == expected


def test_callers_handle_absent_methods():
    assert message_to_e2a(_message(None)).method is None
    assert message_to_legacy_agent_dict(_message(None))["req_method"] is None
    assert e2a_from_agent_fields(request_id="method-test").method is None


def test_method_value_rejects_none():
    with pytest.raises(TypeError):
        method_value(None)


@pytest.mark.parametrize(
    "method,error",
    [(0, TypeError), (False, TypeError), (1, TypeError), (True, TypeError),
     ([], TypeError), ({}, TypeError), (b"chat.send", TypeError),
     (SimpleNamespace(value="chat.send"), TypeError),
     ("", ValueError), (" ", ValueError), ("\t\n", ValueError)],
)
def test_invalid_methods_are_rejected_including_legacy_fallback(method, error):
    with pytest.raises(error):
        method_value(method)
    with pytest.raises(error):
        message_to_e2a(_message(method))
    with pytest.raises(error):
        e2a_from_agent_fields(request_id="method-test", req_method=method)
    with pytest.raises(error):
        message_to_legacy_agent_dict(_message(method))
    with pytest.raises(error):
        message_to_e2a_or_fallback(_message(method))


def test_unrelated_normalization_failure_keeps_legacy_fallback():
    message = _message(_Method.CHAT_SEND)
    message.id = ""
    envelope = message_to_e2a_or_fallback(message)
    legacy = envelope.channel_context[E2A_INTERNAL_CONTEXT_KEY][E2A_LEGACY_AGENT_REQUEST_KEY]
    assert legacy["req_method"] == "chat.send"
