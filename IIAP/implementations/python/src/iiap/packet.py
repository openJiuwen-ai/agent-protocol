"""IntentContextPacket validation against the packaged wire contract."""

from __future__ import annotations

import json
from functools import lru_cache
from importlib.resources import files
from typing import Any

from jsonschema import Draft202012Validator, FormatChecker
from jsonschema.exceptions import ValidationError


@lru_cache(maxsize=1)
def _packet_validator() -> Draft202012Validator:
    resource = files("iiap.schemas").joinpath("intent-context-packet.schema.json")
    schema = json.loads(resource.read_text(encoding="utf-8"))
    return Draft202012Validator(schema, format_checker=FormatChecker())


def validate_intent_context_packet(packet: Any) -> None:
    """Validate a packet or raise ``ValueError('INVALID_PACKET')``.

    The stable error intentionally omits the underlying validation path and
    payload so callers cannot accidentally expose UI context in an HTTP error.
    """

    try:
        json.dumps(packet, allow_nan=False)
        _packet_validator().validate(packet)
    except (ValidationError, TypeError, ValueError, RecursionError) as exc:
        raise ValueError("INVALID_PACKET") from exc
