# Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.

"""The Hub catalog method works without application-side enum registration."""

import json
import subprocess
import sys
from pathlib import Path

from e2a.e2a.agent_compat import e2a_to_agent_request
from e2a.e2a.models import E2AEnvelope
from e2a.message import ReqMethod


def test_hub_catalog_is_available_in_a_clean_protocol_only_process():
    package_root = Path(__file__).resolve().parents[1]
    script = (
        "import sys; "
        f"sys.path.insert(0, {str(package_root)!r}); "
        "from e2a.message import ReqMethod; "
        "assert ReqMethod('hub.expert_packages.catalog') is ReqMethod.HUB_EXPERT_PACKAGES_CATALOG; "
        "assert not any(name.split('.')[0] in ('jiuwenswarm', 'gateway') for name in sys.modules)"
    )
    subprocess.run([sys.executable, "-I", "-c", script], check=True)


def test_hub_catalog_round_trip_needs_no_user_or_session():
    envelope = E2AEnvelope(
        request_id="catalog-request",
        channel="gateway",
        method=ReqMethod.HUB_EXPERT_PACKAGES_CATALOG.value,
        params={},
        is_stream=False,
    )
    wire = json.loads(json.dumps(envelope.to_dict()))
    request = e2a_to_agent_request(E2AEnvelope.from_dict(wire))
    assert request.req_method is ReqMethod.HUB_EXPERT_PACKAGES_CATALOG
    assert request.request_id == envelope.request_id
    assert request.channel_id == "gateway"
    assert not request.session_id
    assert not request.user_id
    assert request.params == {}
    assert request.is_stream is False
