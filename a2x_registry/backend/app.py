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
from a2x_registry.common.errors import FeatureNotInstalledError

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


@app.get("/api/warmup-status")
async def warmup_status():
    """Returns current warmup state for the frontend loading screen."""
    return warmup_state


@app.on_event("startup")
async def _startup():
    # Suppress warmup-status polling from uvicorn access log
    class _SuppressWarmupPoll(logging.Filter):
        def filter(self, record: logging.LogRecord) -> bool:
            return "/api/warmup-status" not in record.getMessage()
    logging.getLogger("uvicorn.access").addFilter(_SuppressWarmupPoll())

    loop = asyncio.get_event_loop()
    loop.run_in_executor(ThreadPoolExecutor(1), run_warmup)


# Optional static mount for the clone-source demo UI. When the source tree's
# ``ui/launcher.py`` is used, it exports ``A2X_FRONTEND_DIST_DIR`` pointing at
# ``ui/frontend/dist/`` before starting uvicorn. Pip users never set this and
# the backend stays API-only.
_frontend_dist = os.environ.get("A2X_FRONTEND_DIST_DIR")
if _frontend_dist and Path(_frontend_dist).is_dir():
    app.mount("/", StaticFiles(directory=_frontend_dist, html=True), name="frontend")
