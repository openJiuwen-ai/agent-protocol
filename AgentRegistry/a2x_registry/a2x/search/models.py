"""Data models for A2X hierarchical taxonomy search."""

import threading
from dataclasses import dataclass, field
from typing import List, Set

from a2x_registry.common.models import SearchResult  # noqa: F401 — re-exported


@dataclass
class SearchStats:
    """Thread-safe statistics accumulated during search."""
    llm_calls: int = 0
    total_tokens: int = 0
    visited_categories: List[str] = field(default_factory=list)
    pruned_categories: List[str] = field(default_factory=list)
    visited_category_ids: List[str] = field(default_factory=list)
    _lock: threading.Lock = field(default_factory=threading.Lock, repr=False)

    def update(self, llm_calls: int = 0, tokens: int = 0,
               visited: List[str] = None, pruned: List[str] = None,
               visited_ids: List[str] = None):
        with self._lock:
            self.llm_calls += llm_calls
            self.total_tokens += tokens
            if visited:
                self.visited_categories.extend(visited)
            if pruned:
                self.pruned_categories.extend(pruned)
            if visited_ids:
                self.visited_category_ids.extend(visited_ids)


@dataclass
class NavigationStep:
    """One step of category navigation for UI animation."""
    parent_id: str
    selected: List[str]
    pruned: List[str]


@dataclass
class TerminalNode:
    """A leaf node reached during Phase 1 navigation."""
    category_id: str
    service_ids: List[str]


@dataclass
class ServiceGroup:
    """A group of services for Phase 2 selection."""
    leaf_ids: Set[str]
    service_ids: List[str]
