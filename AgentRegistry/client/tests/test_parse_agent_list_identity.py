# coding: utf-8
"""AgentCard extensions must not shadow registry identity in list_agents."""

import httpx

from a2x_registry_client._internal import parse_agent_list


def _row():
    body = [{
        "id": "agent_real_id",
        "type": "a2a",
        "name": "wrapper name",
        "description": "wrapper description",
        "owner_id": "principal-1",
        "metadata": {
            "id": "card_extension_id",
            "type": "card_extension_type",
            "owner_id": "card_extension_owner",
            "name": "card name",
            "description": "card description",
            "url": "http://card.example",
        },
    }]
    return parse_agent_list(httpx.Response(200, json=body))[0]


def test_card_extensions_do_not_shadow_registry_identity():
    row = _row()
    assert row["id"] == "agent_real_id"
    assert row["type"] == "a2a"
    assert row["owner_id"] == "principal-1"


def test_metadata_still_wins_for_display_and_keeps_card_fields():
    row = _row()
    assert row["name"] == "card name"
    assert row["description"] == "card description"
    assert row["url"] == "http://card.example"


def test_generic_entries_are_unchanged():
    body = [{"id": "g1", "type": "generic", "name": "g"}]
    assert parse_agent_list(httpx.Response(200, json=body)) == body
