"""Search API router — REST endpoint + WebSocket for real-time streaming."""

import asyncio
import json
import logging
from concurrent.futures import ThreadPoolExecutor

from fastapi import APIRouter, WebSocket, WebSocketDisconnect

from a2x_registry.backend.schemas.models import (
    SearchRequest, SearchResponse, JudgeRequest, JudgeResponse, JudgeResult,
)
from a2x_registry.backend.services.search_service import search_service

logger = logging.getLogger(__name__)
router = APIRouter(prefix="/api/search", tags=["search"])

_executor = ThreadPoolExecutor(max_workers=4)


@router.post("", response_model=SearchResponse)
async def search(req: SearchRequest):
    """Synchronous search — returns the full result at once."""
    loop = asyncio.get_event_loop()
    result = await loop.run_in_executor(
        _executor,
        lambda: search_service.search(
            query=req.query,
            method=req.method,
            dataset=req.dataset,
            top_k=req.top_k or 10,
        ),
    )
    return SearchResponse(**result)


@router.post("/judge", response_model=JudgeResponse)
async def judge_relevance(req: JudgeRequest):
    """Use shared LLM to judge whether each service is relevant to the query."""
    loop = asyncio.get_event_loop()
    raw = await loop.run_in_executor(
        _executor,
        lambda: search_service.judge_services(req.query, req.services),
    )
    return JudgeResponse(results=[JudgeResult(**r) for r in raw])


@router.websocket("/ws")
async def search_ws(websocket: WebSocket):
    """WebSocket search with real-time A2X navigation step streaming.

    Client sends: {"query": "...", "method": "...", "dataset": "...", "top_k": 10}
    Server sends a sequence of messages:
        {"type": "step",   "data": {"parent_id": "...", "selected": [...], "pruned": [...]}}
        {"type": "result", "data": {results, stats, elapsed_time}}
    On error:
        {"type": "error",  "message": "..."}
    """
    await websocket.accept()
    try:
        raw = await websocket.receive_text()
        req = json.loads(raw)

        query = req.get("query", "")
        method = req.get("method", "a2x_get_all")
        dataset = req.get("dataset", "ToolRet_clean")
        top_k = req.get("top_k", 10)

        loop = asyncio.get_event_loop()

        if method.startswith("a2x"):
            gen = search_service.search_stream(query=query, method=method, dataset=dataset)

            def next_msg():
                try:
                    return next(gen)
                except StopIteration:
                    return None

            while True:
                msg = await loop.run_in_executor(_executor, next_msg)
                if msg is None:
                    break
                await websocket.send_json({
                    "type": msg["type"],
                    "data": msg,
                })

        else:
            result = await loop.run_in_executor(
                _executor,
                lambda: search_service.search(
                    query=query, method=method, dataset=dataset, top_k=top_k,
                ),
            )
            await websocket.send_json({"type": "result", "data": result})

    except WebSocketDisconnect:
        logger.info("WebSocket client disconnected")
    except Exception as e:
        import traceback
        logger.error("WebSocket error: %s\n%s", e, traceback.format_exc())
        try:
            await websocket.send_json({"type": "error", "message": str(e)})
        except Exception:
            pass
