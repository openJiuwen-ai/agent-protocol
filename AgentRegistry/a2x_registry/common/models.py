"""Shared data models across search methods (a2x, vector, traditional)."""

from dataclasses import dataclass


@dataclass
class SearchResult:
    """A service found by search."""
    id: str
    name: str
    description: str = ""
