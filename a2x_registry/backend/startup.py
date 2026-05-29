"""Backend startup — pre-loads all search engines and taxonomy caches.

Called once during FastAPI app startup. Runs in a background thread so the
server can begin accepting requests (e.g. /api/warmup-status) while loading.
"""

import logging
import time

from a2x_registry.common.paths import database_dir
from a2x_registry.common import feature_flags

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

        # 1b. Auth store — load if bootstrapped, else leave global at None
        # so every request flows through the anonymous fast-path. Loading
        # is synchronous and cheap (one or two small JSON files); never
        # fails the warmup — auth corruption is logged but the server
        # still starts so operators can investigate via /api/warmup-status.
        try:
            from a2x_registry.auth.store import AuthStore
            from a2x_registry.auth.deps import set_auth_store
            auth_store = AuthStore.load_or_none()
            set_auth_store(auth_store)
            if auth_store is not None:
                logger.info(
                    "  Auth store loaded (%d principals, %d keys) at %s",
                    len(auth_store.list_principals()),
                    len(auth_store.list_keys()),
                    auth_store.data_dir,
                )
            else:
                logger.info("  Auth not initialized — registry runs in anonymous mode")
        except Exception as exc:
            logger.error("  Auth store load failed: %s", exc, exc_info=True)
            from a2x_registry.auth.deps import set_auth_store
            set_auth_store(None)

        # 1c. Heartbeat module — always loaded (the per-namespace
        # lease_config.json is the actual opt-in switch; loading the
        # store / sweeper here just makes the machinery available).
        # Failure is non-fatal: backward compat is preserved (services
        # without lease_ttl stay permanent; sweep just doesn't fire).
        try:
            from a2x_registry.heartbeat.store import HeartbeatStore
            from a2x_registry.heartbeat.sweeper import HeartbeatSweeper
            from a2x_registry.heartbeat.deps import set_heartbeat_store
            hb_store = HeartbeatStore(
                config_provider=registry_svc.get_lease_config,
            )
            registry_svc.set_unhealthy_check(hb_store.is_unhealthy)
            # Recover any entries persisted with lease_ttl into a grace
            # window so they get one chance to re-heartbeat after restart.
            recovered = [
                (ds, e.service_id, e.lease_ttl)
                for ds in registry_svc.list_datasets()
                for e in registry_svc.list_entries(ds)
                if e.lease_ttl is not None
            ]
            if recovered:
                hb_store.recover_from_persisted(recovered)
            set_heartbeat_store(hb_store)
            # Sweeper period 5s — see docs/heartbeat_design.md. Daemon
            # thread so it doesn't block server shutdown.
            sweeper = HeartbeatSweeper(registry_svc, hb_store, period=5.0)
            sweeper.start()
            # Stash on warmup_state so tests / shutdown logic can access.
            warmup_state["_heartbeat_sweeper"] = sweeper
            logger.info(
                "  Heartbeat store loaded (recovered %d leases into grace window)",
                len(recovered),
            )
        except Exception as exc:
            logger.error("  Heartbeat init failed: %s", exc, exc_info=True)
            from a2x_registry.heartbeat.deps import set_heartbeat_store
            set_heartbeat_store(None)

        # Only wire vector-sync side effects when the [vector] extras are
        # installed; otherwise every SDK register/deregister would spawn a
        # daemon thread that immediately ImportErrors on chromadb.
        if feature_flags.has("vector"):
            registry_svc.set_on_service_changed(
                lambda ds: search_service.schedule_vector_sync(ds)
            )
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

        # 3. A2X engines — pure LLM, runs on the lite install too.
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

        # Stages 4-6 truly need the [vector] extras (numpy / chromadb /
        # sentence_transformers). On lite installs we skip them cleanly.
        if feature_flags.has("vector"):
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

            # 5b. Vector engines
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
        else:
            logger.info("Warmup: lite install detected — skipping vector / chroma stages")

        warmup_state["stage"] = "完成"
        warmup_state["progress"] = 100
        warmup_state["ready"] = True
        logger.info("Warmup complete — total %.1fs", time.time() - t0)

    except Exception as e:
        import traceback
        warmup_state["error"] = str(e)
        warmup_state["ready"] = True
        logger.error("Warmup error: %s\n%s", e, traceback.format_exc())
