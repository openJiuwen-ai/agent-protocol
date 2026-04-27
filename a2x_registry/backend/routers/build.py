"""FastAPI router for taxonomy build operations.

Handles build trigger, status, cancellation, and SSE log streaming.
"""

import asyncio
import json
import logging
import queue as _queue
import threading
import time
from typing import Optional

from fastapi import APIRouter, BackgroundTasks, HTTPException, Request
from fastapi.responses import StreamingResponse

from a2x_registry.register.models import BuildRequest
from a2x_registry.register.service import RegistryService
from a2x_registry.common import feature_flags

logger = logging.getLogger(__name__)

router = APIRouter(prefix="/api/datasets", tags=["build"])

_service: Optional[RegistryService] = None

# Build state — keyed by dataset name
_build_jobs: dict = {}    # {dataset: {status, message, started_at, finished_at, logs}}
_cancel_flags: dict = {}  # {dataset: threading.Event}  — cancellation signals
_log_subs: dict = {}      # {dataset: [queue.Queue, ...]}  — SSE subscribers per dataset
_subs_lock = threading.Lock()


def _push_to_subs(dataset: str, item: dict) -> None:
    """Push a log/status event to all active SSE subscribers for this dataset."""
    with _subs_lock:
        subs = list(_log_subs.get(dataset, []))
    for q in subs:
        try:
            q.put_nowait(item)
        except _queue.Full:
            pass  # slow consumer — drop rather than block


def init_registry_service(svc: RegistryService) -> None:
    """Inject the shared RegistryService instance. Called once from backend startup."""
    global _service
    _service = svc


def _get_service() -> RegistryService:
    if _service is None:
        raise HTTPException(status_code=503, detail="Registry service not initialized")
    return _service


# ---------------------------------------------------------------------------
# POST /{dataset}/build — trigger taxonomy build
# ---------------------------------------------------------------------------

@router.post("/{dataset}/build")
async def trigger_build(dataset: str, req: BuildRequest, background_tasks: BackgroundTasks):
    """Trigger A2X taxonomy build for a dataset (runs in background)."""
    feature_flags.require("vector")
    if _build_jobs.get(dataset, {}).get("status") == "running":
        raise HTTPException(status_code=409, detail=f"Build already running for '{dataset}'")
    stop_event = threading.Event()
    _cancel_flags[dataset] = stop_event
    _build_jobs[dataset] = {
        "status": "running",
        "message": "构建中，请稍候...",
        "started_at": time.time(),
        "finished_at": None,
        "logs": [],
    }
    extra = {k: v for k, v in req.model_dump().items()
             if k != "resume" and v is not None}
    background_tasks.add_task(_run_taxonomy_build, dataset, req.resume, extra, stop_event)
    return {"dataset": dataset, "status": "started", "message": "构建已启动"}


# ---------------------------------------------------------------------------
# GET /{dataset}/build/status — build status
# ---------------------------------------------------------------------------

@router.get("/{dataset}/build/status")
async def get_build_status(dataset: str):
    """Get current build status for a dataset."""
    return {"dataset": dataset, **_build_jobs.get(dataset, {"status": "idle"})}


# ---------------------------------------------------------------------------
# DELETE /{dataset}/build — cancel build
# ---------------------------------------------------------------------------

@router.delete("/{dataset}/build")
async def cancel_build(dataset: str):
    """Cancel a running build for a dataset."""
    if _build_jobs.get(dataset, {}).get("status") != "running":
        raise HTTPException(status_code=409, detail=f"No running build for '{dataset}'")
    event = _cancel_flags.get(dataset)
    if event:
        event.set()
    msg = "构建已取消"
    _build_jobs[dataset].update({"status": "cancelled", "message": msg, "finished_at": time.time()})
    _push_to_subs(dataset, {"type": "status", "status": "cancelled", "message": msg})
    return {"dataset": dataset, "status": "cancelled", "message": msg}


# ---------------------------------------------------------------------------
# GET /{dataset}/build/stream — SSE build logs
# ---------------------------------------------------------------------------

@router.get("/{dataset}/build/stream")
async def build_stream(dataset: str, request: Request):
    """SSE stream for real-time build logs (text/event-stream).

    On connect: replays all captured logs, then streams new ones live.
    Closes automatically when the build finishes or the client disconnects.
    Each event is a JSON object:
      {"type": "log",    "message": "<formatted log line>"}
      {"type": "status", "status": "done|error|idle", "message": "..."}
    """
    async def generate():
        # Replay captured history first
        job = _build_jobs.get(dataset, {})
        for msg in list(job.get("logs", [])):
            yield f"data: {json.dumps({'type': 'log', 'message': msg})}\n\n"

        # If not running, send final status and close
        if job.get("status") != "running":
            status = job.get("status", "idle")
            yield f"data: {json.dumps({'type': 'status', 'status': status, 'message': job.get('message', '')})}\n\n"
            return

        # Register as a subscriber for this dataset
        q: _queue.Queue = _queue.Queue(maxsize=1000)
        with _subs_lock:
            _log_subs.setdefault(dataset, []).append(q)

        try:
            loop = asyncio.get_event_loop()
            while True:
                if await request.is_disconnected():
                    break
                try:
                    item = await loop.run_in_executor(None, lambda: q.get(timeout=1.0))
                    yield f"data: {json.dumps(item)}\n\n"
                    if item.get("type") == "status":
                        break  # build finished — close stream
                except _queue.Empty:
                    # Re-check in case build finished without pushing a sentinel
                    cur = _build_jobs.get(dataset, {})
                    if cur.get("status") != "running":
                        yield f"data: {json.dumps({'type': 'status', 'status': cur.get('status', 'idle'), 'message': cur.get('message', '')})}\n\n"
                        break
                    yield ": keepalive\n\n"  # SSE comment — browser ignores, keeps connection alive
        finally:
            with _subs_lock:
                subs = _log_subs.get(dataset, [])
                if q in subs:
                    subs.remove(q)

    return StreamingResponse(
        generate(),
        media_type="text/event-stream",
        headers={"Cache-Control": "no-cache", "X-Accel-Buffering": "no"},
    )


# ---------------------------------------------------------------------------
# Background task: taxonomy builder
# ---------------------------------------------------------------------------

def _run_taxonomy_build(dataset: str, resume: str, extra_params: dict = None,
                        stop_event: threading.Event = None) -> None:
    """Background task: run A2X taxonomy builder for the given dataset."""
    job = _build_jobs[dataset]
    _thread_id = threading.current_thread().ident

    class _LogCapture(logging.Handler):
        def emit(self, record):
            # Filter to this build's thread only — prevents cross-dataset contamination
            # when multiple datasets build simultaneously (all share the a2x_registry.a2x logger)
            if record.thread != _thread_id:
                return
            try:
                msg = self.format(record)
                job["logs"].append(msg)
                _push_to_subs(dataset, {"type": "log", "message": msg})
            except Exception:
                pass

    handler = _LogCapture()
    handler.setFormatter(logging.Formatter("%(asctime)s  %(message)s", datefmt="%H:%M:%S"))
    # Attach directly to a2x_registry.a2x logger — captures all build output without root-level filtering.
    # Thread-ID filter above ensures concurrent builds don't receive each other's log records.
    a2x_logger = logging.getLogger("a2x_registry.a2x")
    # Use caller-specified log level, otherwise follow the logger's effective level
    log_level_name = (extra_params or {}).pop("log_level", None)
    handler.setLevel(getattr(logging, (log_level_name or "").upper(), a2x_logger.getEffectiveLevel()))
    a2x_logger.addHandler(handler)

    try:
        from a2x_registry.a2x.build.config import AutoHierarchicalConfig
        from a2x_registry.a2x.build.taxonomy_builder import TaxonomyBuilder
        service_path = _get_service().service_json_path(dataset)
        if not service_path.exists():
            msg = (
                f"No services registered for dataset '{dataset}' yet "
                "(service.json missing). Register at least one service first."
            )
            job.update({"status": "error", "message": msg, "finished_at": time.time()})
            _push_to_subs(dataset, {"type": "status", "status": "error", "message": msg})
            return
        config = AutoHierarchicalConfig(service_path=str(service_path), **(extra_params or {}))
        builder = TaxonomyBuilder(config, stop_event=stop_event)
        builder.build(resume=resume)
        # If cancel_build() already updated the job, don't overwrite with "done"
        if job.get("status") == "running":
            job.update({"status": "done", "message": "分类树构建完成", "finished_at": time.time()})
            _push_to_subs(dataset, {"type": "status", "status": "done", "message": "分类树构建完成"})
    except InterruptedError:
        pass  # cancel_build() already set status and pushed SSE event
    except Exception as e:
        msg = str(e)
        job.update({"status": "error", "message": msg, "finished_at": time.time()})
        _push_to_subs(dataset, {"type": "status", "status": "error", "message": msg})
        logger.error("Taxonomy build error for %s: %s", dataset, e, exc_info=True)
    finally:
        a2x_logger.removeHandler(handler)
