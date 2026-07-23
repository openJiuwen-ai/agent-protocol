"""FastAPI application — A2X Registry backend."""

import asyncio
import logging
import os
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

from fastapi import FastAPI, Request
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import JSONResponse
from fastapi.staticfiles import StaticFiles

from a2x_registry.backend.routers import search, dataset, build, provider
from a2x_registry.backend.startup import warmup_state, run_warmup
from a2x_registry.common.errors import FeatureNotInstalledError, LLMNotConfiguredError
from a2x_registry.auth.router import router as auth_router
from a2x_registry.heartbeat.router import router as heartbeat_router
from a2x_registry.heartbeat.router import node_router as node_heartbeat_router
from a2x_registry.cluster.router import router as cluster_router
from a2x_registry.image.router import router as image_router
from a2x_registry.instance.router import router as instance_router

app = FastAPI(
    title="A2X Registry Demo",
    description="Interactive comparison of A2X hierarchical search vs vector retrieval",
    version="1.3.0",
)

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=False,
    allow_methods=["*"],
    allow_headers=["*"],
)

app.include_router(search.router)
app.include_router(dataset.router)
app.include_router(build.router)
app.include_router(provider.router)
# /api/auth/* — the router itself returns 404 when auth is not initialized,
# so mounting it unconditionally is safe and keeps the app graph simple.
app.include_router(auth_router)
# Heartbeat endpoints. Same 404 fallback semantics when the heartbeat
# module isn't initialized (e.g. lite mode without startup hook).
app.include_router(heartbeat_router)
# Per-node heartbeat endpoints (appliance mode). Same 404 fallback
# semantics when the per-node heartbeat module isn't assembled.
app.include_router(node_heartbeat_router)
# Cluster (distributed sync) endpoints. Opt-in: every route 404s until the
# cluster module is initialized (cluster_state.json present at startup).
app.include_router(cluster_router)
# Image management endpoints. Accessible only after appliance-mode
# assembly; when not assembled, _resolve_service in the router returns
# 404 (same fallback semantics as heartbeat/cluster).
app.include_router(image_router)
# Instance management endpoints. Accessible only after appliance-mode
# assembly; when not assembled, _resolve_service in the router returns
# 404 (same fallback semantics as heartbeat/cluster/image).
app.include_router(instance_router)


@app.exception_handler(FeatureNotInstalledError)
async def _feature_not_installed_handler(_request: Request, exc: FeatureNotInstalledError):
    """Render missing-extras failures as 503 with a structured install hint.

    Routes hit by the SDK never reach here (they don't gate behind extras).
    Heavy routes (search/build) raise this from their entry-point
    ``feature_flags.require()`` call when the corresponding extras are
    not installed. The body's ``detail`` is a copy-pasteable pip command.
    """
    return JSONResponse(
        status_code=503,
        content={
            "feature": exc.feature,
            "extras": exc.extras,
            "detail": str(exc),
        },
    )


@app.exception_handler(LLMNotConfiguredError)
async def _llm_not_configured_handler(_request: Request, exc: LLMNotConfiguredError):
    """Render missing LLM-config failures as 503 with a structured setup hint.

    Routes that need the shared LLM client (``/api/search``, ``/api/search/judge``,
    background build tasks) raise this when ``llm_apikey.json`` is absent or
    unparseable. ``str(exc)`` already contains the copy-pasteable setup
    instructions; surface them raw.
    """
    return JSONResponse(
        status_code=503,
        content={
            "reason": "llm_not_configured",
            "detail": str(exc),
        },
    )


@app.get("/api/warmup-status")
async def warmup_status():
    """Returns current warmup state for the frontend loading screen.

    Only the public, JSON-serializable status fields are exposed.
    ``warmup_state`` doubles as a stash for background-daemon handles
    (``_heartbeat_sweeper`` / ``_cluster_*``) so shutdown logic can reach
    them; those objects hold ``threading.Lock``s and would make
    ``jsonable_encoder`` raise. Underscore-prefixed keys are internal —
    filter them out so the endpoint can't 500.
    """
    return {k: v for k, v in warmup_state.items() if not k.startswith("_")}


@app.on_event("startup")
async def _startup():
    # Suppress warmup-status polling from uvicorn access log
    class _SuppressWarmupPoll(logging.Filter):
        def filter(self, record: logging.LogRecord) -> bool:
            return "/api/warmup-status" not in record.getMessage()
    logging.getLogger("uvicorn.access").addFilter(_SuppressWarmupPoll())

    loop = asyncio.get_event_loop()
    loop.run_in_executor(ThreadPoolExecutor(1), run_warmup)


@app.on_event("shutdown")
async def _shutdown():
    # Stop background daemons BEFORE releasing the pool/transport, so a
    # sweeper tick can't submit to an already-shut-down pool or use a closed
    # HTTP client.
    from a2x_registry.backend.startup import warmup_state
    for key in (
        "_cluster_anti_entropy", "_cluster_keepalive",
        "_heartbeat_sweeper", "_node_heartbeat_sweeper",
    ):
        sweeper = warmup_state.get(key)
        if sweeper is not None:
            try:
                sweeper.stop()
            except Exception:  # noqa: BLE001 — shutdown must not raise
                pass
    # Release the cluster fan-out pool + pooled HTTP connections, if loaded.
    try:
        from a2x_registry.cluster.deps import get_cluster_store
        store = get_cluster_store()
        if store is not None:
            store.close()
    except Exception:  # noqa: BLE001 — shutdown must not raise
        pass


# Optional static mount for the clone-source demo UI. When the source tree's
# ``ui/launcher.py`` is used, it exports ``A2X_FRONTEND_DIST_DIR`` pointing at
# ``ui/frontend/dist/`` before starting uvicorn. Pip users never set this and
# the backend stays API-only.
_frontend_dist = os.environ.get("A2X_FRONTEND_DIST_DIR")
if _frontend_dist and Path(_frontend_dist).is_dir():
    app.mount("/", StaticFiles(directory=_frontend_dist, html=True), name="frontend")
