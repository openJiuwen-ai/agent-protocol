"""Unified search service — facade over A2X, Vector, and Traditional search."""

import json
import threading
import time
import logging
from pathlib import Path
from typing import Dict, Generator, List, Optional

from a2x_registry.common import feature_flags

logger = logging.getLogger(__name__)

# Optional reference to RegistryService for taxonomy state checks.
# Set via set_registry() during app startup.
_registry = None


def set_registry(registry) -> None:
    """Bind the RegistryService so A2X searches can check taxonomy state."""
    global _registry
    _registry = registry


def _check_taxonomy(dataset: str) -> None:
    """Raise ValueError if the dataset's taxonomy is unavailable or nonexistent.

    Only checked for registry-managed datasets (those _registry knows about).
    Unmanaged datasets (e.g. ToolRet_clean pre-built) are always allowed through.
    """
    if _registry is None:
        return
    from a2x_registry.register.models import TaxonomyState
    state = _registry.check_taxonomy_state(dataset)
    if state is None:
        return  # not registry-managed — no restriction
    if state == TaxonomyState.UNAVAILABLE:
        raise ValueError(
            f"Dataset '{dataset}' 的分类树已过时（services 已变更），请先重新 build 再搜索"
        )
    if state == TaxonomyState.NONEXISTENT:
        raise ValueError(
            f"Dataset '{dataset}' 尚未构建分类树，请先运行 build"
        )


def _require_registry():
    """Resolve the bound registry or fail loudly (these path-resolving
    helpers are useless without it)."""
    if _registry is None:
        raise RuntimeError(
            "SearchService called before set_registry(); call init_registry_service "
            "in the dataset router and search_service.set_registry on startup."
        )
    return _registry


def discover_datasets() -> List[str]:
    """Return names of all datasets that have both service.json and query.json."""
    reg = _require_registry()
    return [
        info["name"] for info in reg.list_datasets_with_counts()
        if reg.query_path(info["name"]).exists()
    ]


def resolve_dataset_paths(dataset: str) -> Dict[str, Path]:
    """Resolve file paths for a given dataset name."""
    reg = _require_registry()
    return {
        "service_path": reg.service_json_path(dataset),
        "query_path": reg.query_path(dataset),
        "taxonomy_path": reg.taxonomy_path(dataset),
        "class_path": reg.class_path(dataset),
    }


class SearchService:
    """Manages search engine instances and provides a unified search interface.

    Thread-safe: all instance caches are protected by a single RLock.
    """

    def __init__(self):
        self._lock = threading.RLock()
        self._a2x_instances: Dict[str, object] = {}
        self._vector_instances: Dict[str, object] = {}
        self._traditional_instances: Dict[str, object] = {}
        self._embedding_models: Dict[str, object] = {}  # keyed by model name
        self._llm_client = None        # Shared LLM client for judge/other non-search calls
        self._current_provider: str = ""

    # ── Provider management ──────────────────────────────────────────────────

    def get_current_provider(self) -> str:
        return self._current_provider

    def switch_provider(self, name: str) -> None:
        """Switch the active LLM provider and reset all A2X engine instances."""
        self.reset_a2x()
        self._current_provider = name

    # ── Engine accessors (lazy-load, cached) ─────────────────────────────────

    @staticmethod
    def read_vector_config(dataset: str) -> str:
        """Read the embedding-model name for a dataset (default if missing)."""
        return _require_registry().get_vector_config(dataset)["embedding_model"]

    def _get_embedding_model(self, model_name: str | None = None):
        """Get or create an EmbeddingModel by name (cached by model name)."""
        from a2x_registry.vector.utils.embedding_constants import DEFAULT_EMBEDDING_MODEL
        name = model_name or DEFAULT_EMBEDDING_MODEL
        with self._lock:
            if name not in self._embedding_models:
                from a2x_registry.vector.utils.embedding import EmbeddingModel
                self._embedding_models[name] = EmbeddingModel(name)
            return self._embedding_models[name]

    def get_embedding_model_for_dataset(self, dataset: str):
        """Resolve the dataset's configured embedding model and return the instance."""
        return self._get_embedding_model(self.read_vector_config(dataset))

    def _get_llm_client(self):
        with self._lock:
            if self._llm_client is None:
                from a2x_registry.common.llm_client import LLMClient
                self._llm_client = LLMClient()
            return self._llm_client

    def _get_a2x(self, dataset: str, mode: str):
        # A2X is pure LLM (no embeddings); runs on the lite install.
        key = f"{dataset}_{mode}"
        with self._lock:
            if key not in self._a2x_instances:
                from a2x_registry.a2x.search.a2x_search import A2XSearch
                paths = resolve_dataset_paths(dataset)
                self._a2x_instances[key] = A2XSearch(
                    taxonomy_path=str(paths["taxonomy_path"]),
                    class_path=str(paths["class_path"]),
                    service_path=str(paths["service_path"]),
                    max_workers=20,
                    parallel=True,
                    mode=mode,
                )
            return self._a2x_instances[key]

    def _get_traditional(self, dataset: str):
        # Traditional (MCP-style full-context) is pure LLM; runs on lite.
        with self._lock:
            if dataset not in self._traditional_instances:
                from a2x_registry.traditional.search.traditional_search import TraditionalSearch
                paths = resolve_dataset_paths(dataset)
                self._traditional_instances[dataset] = TraditionalSearch(
                    service_path=str(paths["service_path"]),
                )
            return self._traditional_instances[dataset]

    def _get_vector(self, dataset: str):
        feature_flags.require("vector")
        with self._lock:
            if dataset not in self._vector_instances:
                from a2x_registry.vector.search.vector_search import VectorSearch
                paths = resolve_dataset_paths(dataset)
                collection = dataset.lower().replace("-", "_")
                model_name = self.read_vector_config(dataset)
                self._vector_instances[dataset] = VectorSearch(
                    service_path=str(paths["service_path"]),
                    collection_name=collection,
                    model_name=model_name,
                    model=self._get_embedding_model(model_name),
                )
            return self._vector_instances[dataset]

    def reset_a2x(self) -> None:
        """Clear all A2X and LLM instances (e.g. after provider switch)."""
        with self._lock:
            self._a2x_instances.clear()
            self._llm_client = None

    def purge_dataset(self, dataset: str) -> None:
        """Drop all search-side state for a dataset (cached instances + Chroma).

        Called from the dataset-delete endpoint so the router doesn't have
        to know about ChromaDB internals or the cache dict layout.
        Best-effort: ChromaDB clear failures are logged but not raised
        (the caller is mid-delete and shouldn't be blocked by side state).

        Lite-safe: when ``[vector]`` is not installed, returns immediately —
        there's nothing cached and no chroma collection to clear, so SDK
        ``delete_dataset`` stays silent (no ImportError log noise).
        """
        if not feature_flags.has("vector"):
            return
        # Clear ChromaDB collection (idempotent — collection may not exist)
        try:
            from a2x_registry.vector.utils.chroma_store import ChromaStore
            collection = dataset.lower().replace("-", "_")
            chroma_dir = str(_require_registry().chroma_dir())
            ChromaStore(collection, chroma_dir).clear()
            logger.info("Cleared ChromaDB collection: %s", collection)
        except Exception as e:
            logger.warning("Failed to clear ChromaDB for %s: %s", dataset, e)

        # Drop cached search instances (vector / a2x variants / traditional)
        with self._lock:
            self._vector_instances.pop(dataset, None)
            for key in list(self._a2x_instances):
                if key.startswith(f"{dataset}_"):
                    self._a2x_instances.pop(key, None)
            self._traditional_instances.pop(dataset, None)

    # ── Vector sync ──────────────────────────────────────────────────────────

    def sync_vector(self, dataset: str) -> None:
        """Incrementally sync the ChromaDB index with current service.json.

        Only re-encodes services whose description changed or are new.
        Removes IDs no longer in service.json.
        If the configured embedding model differs from the stored one, does a full rebuild.
        Invalidates the cached VectorSearch instance if anything changed.
        """
        paths = resolve_dataset_paths(dataset)
        service_path = paths["service_path"]
        if not service_path.exists():
            logger.warning("sync_vector: service.json not found for %s", dataset)
            return

        with open(service_path, encoding="utf-8") as f:
            services = json.load(f)

        target = {s["id"]: s.get("description", "") for s in services}
        if not target:
            logger.info("sync_vector: no services in %s, skipping", dataset)
            return

        model_name = self.read_vector_config(dataset)

        from a2x_registry.vector.utils.chroma_store import ChromaStore
        collection = dataset.lower().replace("-", "_")
        chroma_dir = str(_require_registry().chroma_dir())
        store = ChromaStore(collection, chroma_dir, embedding_model=model_name)

        # Detect embedding model mismatch → full rebuild
        stored_model = store.stored_embedding_model
        full_rebuild = stored_model is not None and stored_model != model_name
        if full_rebuild:
            logger.info("sync_vector: model changed %s → %s for %s, full rebuild",
                        stored_model, model_name, dataset)
            store.clear()
            # Recreate collection with updated metadata
            store = ChromaStore(collection, chroma_dir, embedding_model=model_name)

        existing = store.get_all_docs()  # {id: text}
        to_delete = [id_ for id_ in existing if id_ not in target]
        to_upsert = {id_: desc for id_, desc in target.items()
                     if id_ not in existing or existing[id_] != desc}

        if not to_delete and not to_upsert:
            logger.info("sync_vector: %s already up-to-date (%d docs)", dataset, len(target))
            return

        if to_delete:
            store.delete_ids(to_delete)
            logger.info("sync_vector: deleted %d docs from %s", len(to_delete), dataset)

        if to_upsert:
            model = self._get_embedding_model(model_name)
            ids = list(to_upsert.keys())
            texts = list(to_upsert.values())
            embeddings = model.encode(texts, show_progress=False).tolist()
            store.upsert(ids=ids, texts=texts, embeddings=embeddings)
            logger.info("sync_vector: upserted %d docs in %s", len(to_upsert), dataset)

        # Invalidate cached VectorSearch — it holds a stale service list
        with self._lock:
            self._vector_instances.pop(dataset, None)

    def schedule_vector_sync(self, dataset: str) -> None:
        """Launch a background daemon thread to sync the vector index.

        Lite-safe: returns early without spawning a thread when ``[vector]``
        is not installed (the on_service_changed callback registered in
        startup.py is also skipped in that case, but this guard is a
        defense in depth).
        """
        if not feature_flags.has("vector"):
            return
        t = threading.Thread(target=self._sync_vector_safe, args=(dataset,), daemon=True)
        t.start()

    def _sync_vector_safe(self, dataset: str) -> None:
        try:
            self.sync_vector(dataset)
        except Exception as e:
            logger.error("sync_vector failed for %s: %s", dataset, e, exc_info=True)

    # ── Search ───────────────────────────────────────────────────────────────

    def judge_services(self, query: str, services: list) -> list:
        """Use shared LLM to judge service relevance. Returns list of {service_id, relevant}."""
        llm = self._get_llm_client()

        svc_list = "\n".join(
            f"{i+1}. {s.name}: {s.description[:200]}"
            for i, s in enumerate(services)
        )
        prompt = (
            f"Given the user query, judge which services are relevant "
            f"(can help fulfill any part of the query) and which are irrelevant.\n\n"
            f"Query: {query}\n\nServices:\n{svc_list}\n\n"
            f'Return ONLY the numbers of RELEVANT services, separated by commas '
            f'(e.g. "1,3,5"). Return "NONE" if no service is relevant.'
        )

        resp = llm.call([{"role": "user", "content": prompt}], temperature=0.0, max_tokens=200)

        relevant_ids = set()
        if resp.content.strip().upper() != "NONE":
            for part in resp.content.replace(",", " ").split():
                try:
                    num = int(part.strip())
                    if 1 <= num <= len(services):
                        relevant_ids.add(services[num - 1].id)
                except ValueError:
                    continue

        return [
            {"service_id": s.id, "relevant": s.id in relevant_ids}
            for s in services
        ]

    def search(self, query: str, method: str, dataset: str = "ToolRet_clean",
               top_k: int = 10) -> Dict:
        """Execute search and return unified response dict (no streaming)."""
        start = time.time()

        if method.startswith("a2x"):
            _check_taxonomy(dataset)
            mode = method.replace("a2x_", "")
            results, stats = self._get_a2x(dataset, mode).search(query)
            stats_dict = {
                "llm_calls": stats.llm_calls,
                "total_tokens": stats.total_tokens,
                "visited_categories": len(stats.visited_categories),
                "pruned_categories": len(stats.pruned_categories),
            }
        elif method == "vector":
            results, stats = self._get_vector(dataset).search(query, top_k=top_k)
            stats_dict = {"llm_calls": 0, "total_tokens": 0}
        elif method == "traditional":
            results, stats = self._get_traditional(dataset).search(query)
            stats_dict = {"llm_calls": stats.llm_calls, "total_tokens": stats.total_tokens}
        else:
            raise ValueError(f"Unknown method: {method}")

        return {
            "results": [{"id": r.id, "name": r.name, "description": r.description}
                        for r in results],
            "stats": stats_dict,
            "elapsed_time": round(time.time() - start, 2),
        }

    def search_stream(self, query: str, method: str,
                      dataset: str = "ToolRet_clean") -> Generator[Dict, None, None]:
        """Streaming search for A2X methods. Yields step dicts then final result."""
        _check_taxonomy(dataset)
        start = time.time()
        mode = method.replace("a2x_", "")
        searcher = self._get_a2x(dataset, mode)

        for msg in searcher.search(query, stream=True):
            if msg["type"] == "step":
                yield msg
            elif msg["type"] == "result":
                msg["elapsed_time"] = round(time.time() - start, 2)
                yield msg


# Module-level singleton — shared by all routers and startup code.
search_service = SearchService()
