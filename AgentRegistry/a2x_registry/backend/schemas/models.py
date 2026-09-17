"""Pydantic models for API request/response schemas."""

from typing import List, Dict, Optional
from pydantic import BaseModel


# --- Request models ---

class SearchRequest(BaseModel):
    query: str
    method: str          # "a2x_get_all", "a2x_get_one", "vector"
    dataset: str = "ToolRet_clean"
    top_k: Optional[int] = 10  # Only for vector method


# --- Response models ---

class ServiceResult(BaseModel):
    id: str
    name: str
    description: str


class SearchResponse(BaseModel):
    results: List[ServiceResult]
    stats: Dict
    elapsed_time: float


class DatasetInfo(BaseModel):
    name: str
    service_count: int
    query_count: int


class JudgeRequest(BaseModel):
    query: str
    services: List[ServiceResult]  # All unique services from comparison


class JudgeResult(BaseModel):
    service_id: str
    relevant: bool


class JudgeResponse(BaseModel):
    results: List[JudgeResult]


class DefaultQuery(BaseModel):
    query: str           # Chinese query
    query_en: str = ""   # English query (for bilingual display)
