# Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.

"""E2A carries method names as strings without an implementation vocabulary."""

import json
import subprocess
import sys
from typing import get_type_hints
from pathlib import Path

from e2a.agent_compat import e2a_to_agent_request
from e2a.models import E2AEnvelope

HUB_CATALOG_METHOD = "hub.expert_packages.catalog"


def test_hub_catalog_is_available_in_a_clean_protocol_only_process():
    """The protocol package needs no method enum of its own."""
    package_root = Path(__file__).resolve().parents[1]
    script = (
        "import sys; "
        f"sys.path.insert(0, {str(package_root)!r}); "
        "import e2a.message as m; "
        "assert not hasattr(m, 'ReqMethod'); "
        "assert not any(name.split('.')[0] in ('jiuwenswarm', 'gateway') for name in sys.modules)"
    )
    subprocess.run([sys.executable, "-I", "-c", script], check=True)


def test_hub_catalog_round_trip_needs_no_user_or_session():
    envelope = E2AEnvelope(
        request_id="catalog-request",
        channel="gateway",
        method=HUB_CATALOG_METHOD,
        params={},
        is_stream=False,
    )
    wire = json.loads(json.dumps(envelope.to_dict()))
    request = e2a_to_agent_request(E2AEnvelope.from_dict(wire))
    assert type(request.req_method) is str
    assert request.req_method == HUB_CATALOG_METHOD
    assert request.request_id == envelope.request_id
    assert request.channel_id == "gateway"
    assert not request.session_id
    assert not request.user_id
    assert request.params == {}
    assert request.is_stream is False


def test_method_annotations_resolve_without_implementation_imports():
    from e2a.agent_models import AgentRequest
    from e2a.message import Message

    assert get_type_hints(Message)["req_method"] == str | None
    assert get_type_hints(AgentRequest)["req_method"] == str | None
    assert get_type_hints(e2a_to_agent_request)["return"] is AgentRequest


def test_unknown_method_survives_without_a_resolver():
    """A newer AgentServer's method must pass through an older package intact."""
    envelope = E2AEnvelope(
        request_id="future-request",
        channel="gateway",
        method="chat.brand_new_method",
        params={},
        is_stream=False,
    )
    assert e2a_to_agent_request(envelope).req_method == "chat.brand_new_method"
