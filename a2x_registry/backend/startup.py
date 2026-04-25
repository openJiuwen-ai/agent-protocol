"""Backend startup — pre-loads all search engines and taxonomy caches.

Called once during FastAPI app startup. Runs in a background thread so the
server can begin accepting requests (e.g. /api/warmup-status) while loading.
"""

import logging
import time

from a2x_registry.common.paths import database_dir

DATABASE_DIR = database_dir()

# Mutable warmup state — polled by the frontend loading screen.
warmup_state: dict = {
    "ready": False,
    "stage": "starting",
    "progress": 0,
    "error": None,
}


def run_warmup() -> None:
    """Execute the full startup sequence (blocking — run in a thread pool)."""
    import logging
    from a2x_registry.backend.services.search_service import (
        search_service, set_registry, discover_datasets, resolve_dataset_paths,
    )
    from a2x_registry.backend.routers.dataset import init_registry_service
    from a2x_registry.backend.routers.build import init_registry_service as init_build_service

    logger = logging.getLogger("uvicorn")

    def _stage(msg: str, pct: int) -> None:
        warmup_state["stage"] = msg
        warmup_state["progress"] = pct
        logger.info("Warmup [%d%%] %s", pct, msg)

    try:
        t0 = time.time()

        # 1. Registry
        _stage("注册服务加载...", 5)
        registry_svc = init_registry_service(DATABASE_DIR)
        init_build_service(registry_svc)  # share the same instance with build router
        registry_states = registry_svc.startup()
        for ds, state in registry_states.items():
            count = len(registry_svc.list_services(ds))
            logger.info("  Registry %s: %d services, taxonomy=%s", ds, count, state.value)

        set_registry(registry_svc)
        registry_svc.set_on_service_changed(lambda ds: search_service.schedule_vector_sync(ds))
        logger.info("Warmup: registry done (%.1fs)", time.time() - t0)

        # 2. Taxonomy caches
        _stage("加载分类树...", 20)
        from a2x_registry.backend.services.taxonomy_service import get_taxonomy_tree
        for ds in discover_datasets():
            try:
                get_taxonomy_tree(ds)
                logger.info("  Taxonomy cached: %s", ds)
            except Exception as e:
                logger.warning("  Taxonomy failed for %s: %s", ds, e)
        logger.info("Warmup: taxonomy done (%.1fs)", time.time() - t0)

        # 3. A2X engines
        _stage("加载 A2X 搜索引擎...", 35)
        for ds in discover_datasets():
            paths = resolve_dataset_paths(ds)
            if paths["taxonomy_path"].exists():
                for mode in ("get_one", "get_all", "get_important"):
                    _stage(f"加载 A2X {mode} ({ds})...", 35)
                    try:
                        search_service._get_a2x(ds, mode)
                        logger.info("  A2X %s ready: %s", mode, ds)
                    except Exception as e:
                        logger.warning("  A2X %s failed for %s: %s", mode, ds, e)
        logger.info("Warmup: A2X done (%.1fs)", time.time() - t0)

        # 4. Clean stale ChromaDB collections
        _stage("清理向量数据库...", 58)
        try:
            import chromadb
            chroma_dir = str(DATABASE_DIR / "chroma")
            client = chromadb.PersistentClient(path=chroma_dir)
            active_datasets = set(discover_datasets())
            active_collections = {ds.lower().replace("-", "_") for ds in active_datasets}
            for col in client.list_collections():
                if col.name not in active_collections:
                    client.delete_collection(col.name)
                    logger.info("  Removed stale collection: %s", col.name)
        except Exception as e:
            logger.warning("  ChromaDB cleanup failed: %s", e)

        # 5. Vector sync for registry-managed datasets
        _stage("同步向量数据库...", 62)
        for ds in registry_states:
            _stage(f"同步向量数据库 ({ds})...", 62)
            try:
                search_service.sync_vector(ds)
                logger.info("  Vector sync done: %s", ds)
            except Exception as e:
                logger.warning("  Vector sync failed for %s: %s", ds, e)
        logger.info("Warmup: vector sync done (%.1fs)", time.time() - t0)

        # 5. Vector engines
        _stage("加载向量搜索引擎...", 75)
        for ds in discover_datasets():
            _stage(f"加载向量引擎 ({ds})...", 75)
            try:
                search_service._get_vector(ds)
                logger.info("  Vector ready: %s", ds)
            except Exception as e:
                logger.warning("  Vector failed for %s: %s", ds, e)
        logger.info("Warmup: vector done (%.1fs)", time.time() - t0)

        # 6. Pre-heat embedding models (one per unique model across datasets)
        _stage("预热向量模型...", 90)
        seen_models = set()
        for ds in discover_datasets():
            model_name = search_service.read_vector_config(ds)
            if model_name not in seen_models:
                try:
                    model = search_service._get_embedding_model(model_name)
                    model.encode("warmup query", show_progress=False)
                    logger.info("  Embedding model pre-heated: %s", model_name)
                    seen_models.add(model_name)
                except Exception as e:
                    logger.warning("  Embedding warmup failed for %s: %s", model_name, e)

        warmup_state["stage"] = "完成"
        warmup_state["progress"] = 100
        warmup_state["ready"] = True
        logger.info("Warmup complete — total %.1fs", time.time() - t0)

    except Exception as e:
        import traceback
        warmup_state["error"] = str(e)
        warmup_state["ready"] = True
        logger.error("Warmup error: %s\n%s", e, traceback.format_exc())
