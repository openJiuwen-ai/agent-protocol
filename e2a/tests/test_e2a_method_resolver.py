# Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.

"""E2A carries method names as plain strings; enum resolution is opt-in.

The method vocabulary lives on the implementation side, so this package must
convert envelopes without knowing any concrete method enum.
"""

import json
import subprocess
import sys
from enum import Enum
from pathlib import Path

from e2a.agent_compat import e2a_to_agent_request
from e2a.models import E2AEnvelope


class _Method(Enum):
    HUB_CATALOG = "hub.expert_packages.catalog"


def test_e2a_import_does_not_pull_the_message_module():
    """The e2a subpackage must not depend on the wire message/enum module."""
    package_root = Path(__file__).resolve().parents[1]
    script = (
        "import sys; "
        f"sys.path.insert(0, {str(package_root)!r}); "
        "import e2a.agent_compat as ac; "
        "import e2a.gateway_normalize as gn; "
        "assert 'ReqMethod' not in dir(ac) and 'ReqMethod' not in dir(gn); "
        "assert not any(n.startswith('jiuwenswarm') or n.startswith('agent_gateway') "
        "for n in sys.modules)"
    )
    subprocess.run([sys.executable, "-I", "-c", script], check=True)


def test_round_trip_preserves_the_wire_method_string():
    envelope = E2AEnvelope(
        request_id="catalog-request",
        channel="gateway",
        method="hub.expert_packages.catalog",
        params={},
        is_stream=False,
    )
    wire = json.loads(json.dumps(envelope.to_dict()))
    request = e2a_to_agent_request(E2AEnvelope.from_dict(wire))
    assert request.req_method == "hub.expert_packages.catalog"
    assert request.request_id == envelope.request_id
    assert request.channel_id == "gateway"
    assert not request.session_id
    assert not request.user_id
    assert request.params == {}
    assert request.is_stream is False


def test_injected_resolver_returns_enum_members():
    envelope = E2AEnvelope(
        request_id="catalog-request",
        channel="gateway",
        method="hub.expert_packages.catalog",
        params={},
    )
    request = e2a_to_agent_request(
        E2AEnvelope.from_dict(envelope.to_dict()), method_resolver=_Method
    )
    assert request.req_method is _Method.HUB_CATALOG


def test_unknown_methods_survive_resolver_injection():
    """A resolver that rejects a name must not fail the conversion."""
    envelope = E2AEnvelope(
        request_id="extension-request",
        channel="gateway",
        method="future.extension",
        params={},
    )
    request = e2a_to_agent_request(
        E2AEnvelope.from_dict(envelope.to_dict()), method_resolver=_Method
    )
    assert request.req_method == "future.extension"
