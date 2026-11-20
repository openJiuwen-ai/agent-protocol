# Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.

from e2a.runtime_http_routes import (
    RUNTIME_HTTP_API_PREFIX,
    RUNTIME_HTTP_CHAT_ROUTES,
    RUNTIME_HTTP_ROUTES,
    resolve_runtime_http_route,
)


def test_declared_runtime_http_routes_are_resolvable():
    routes = RUNTIME_HTTP_ROUTES + RUNTIME_HTTP_CHAT_ROUTES
    assert len({route.method for route in routes}) == len(routes)
    for route in routes:
        assert resolve_runtime_http_route(route.method) == route
        assert route.path.startswith("/")
        assert not route.path.startswith(f"{RUNTIME_HTTP_API_PREFIX}/")


def test_chat_and_history_keep_their_existing_stream_addresses():
    for is_stream in (False, True):
        assert resolve_runtime_http_route("chat.send", is_stream=is_stream)[1:] == (
            "POST", "/chat/completions",
        )
        assert resolve_runtime_http_route("chat.resume", is_stream=is_stream)[1:] == (
            "POST", "/chat/resume",
        )

    assert resolve_runtime_http_route("history.get")[1:] == (
        "GET", "/sessions/{session_id}/history",
    )
    assert resolve_runtime_http_route("history.get", is_stream=True)[1:] == (
        "GET", "/sessions/{session_id}/history/stream",
    )
    assert resolve_runtime_http_route("session.list", is_stream=True) == (
        resolve_runtime_http_route("session.list")
    )


def test_runtime_http_route_keeps_legacy_aliases_and_has_no_implicit_fallback():
    assert resolve_runtime_http_route("agents.tools.list")[1:] == (
        resolve_runtime_http_route("agents.tools_list")[1:]
    )
    assert resolve_runtime_http_route("unknown.method") is None
    assert resolve_runtime_http_route("unknown.method", is_stream=True) is None
