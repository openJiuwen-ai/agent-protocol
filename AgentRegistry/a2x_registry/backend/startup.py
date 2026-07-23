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


# ── env var names (storage backend selection) ───────────────────────────
_ENV_DB_KIND = "A2X_REGISTRY_DB_KIND"
_ENV_DB_ENDPOINT = "A2X_REGISTRY_DB_ENDPOINT"
_ENV_DB_AUTH = "A2X_REGISTRY_DB_AUTH"
_VALID_DB_KINDS = ("sqlite", "memory", "rqlite")
_RQLITE_DEFAULT_ENDPOINT = "http://127.0.0.1:4001"


def _resolve_db_config() -> dict:
    """Build the ``connect(cfg)`` dict from ``A2X_REGISTRY_DB_*`` env vars.

    - ``A2X_REGISTRY_DB_KIND`` empty/missing → ``sqlite`` (production
      single-node, file-persisted at ``<home>/database/registry.db``).
    - ``memory`` → in-process ``:memory:`` backend (debug only, lost on
      process exit); no ``path`` key.
    - ``rqlite`` → Raft-replicated backend; ``A2X_REGISTRY_DB_ENDPOINT``
      (default ``http://127.0.0.1:4001``) and ``A2X_REGISTRY_DB_AUTH``
      (``user:pwd``, default empty) configure the rqlite HTTP API.

    Raises ``ValueError`` on an unknown kind so a typo never silently
    falls back to memory. Business code never branches on kind — it only
    sees the ``Backend`` returned by ``connect``.
    """
    import os

    kind = os.environ.get(_ENV_DB_KIND, "").strip() or "memory"
    if kind not in _VALID_DB_KINDS:
        raise ValueError(
            f"unknown A2X_REGISTRY_DB_KIND={kind!r}; "
            f"accepted values: {', '.join(_VALID_DB_KINDS)}"
        )
    if kind == "memory":
        return {"kind": "memory"}
    if kind == "rqlite":
        endpoint = os.environ.get(_ENV_DB_ENDPOINT, "").strip() or _RQLITE_DEFAULT_ENDPOINT
        auth = os.environ.get(_ENV_DB_AUTH, "") or ""
        return {"kind": "rqlite", "endpoint": endpoint, "auth": auth}
    # sqlite (default)
    db_path = database_dir() / "registry.db"
    db_path.parent.mkdir(parents=True, exist_ok=True)
    return {"kind": "sqlite", "path": str(db_path)}


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

        # 0. SQL backend + RegistryTableService (base assembly).
        #    image/instance services pick up ``_table_service`` from
        #    warmup_state instead of re-instantiating their own Backend.
        #    Failure here is fatal — without the SQL layer image/instance
        #    modules have no store to bind to.
        _stage("Initializing SQL backend...", 2)
        try:
            import os
            from a2x_registry.common.db import connect, init_schema
            from a2x_registry.register.service import RegistryTableService

            db_cfg = _resolve_db_config()
            backend = connect(db_cfg)
            init_schema(backend.conn)
            table_svc = RegistryTableService(backend)

            # Register the named registries per startup mode.
            # Generic mode: only the A2X backward-compat ``default``
            # service registry. Appliance mode: also create the image /
            # instance registries so image/instance routes have a target.
            mode = os.environ.get("A2X_REGISTRY_MODE", "").strip()
            table_svc.create_registry("default", "service")
            if mode == "appliance":
                table_svc.create_registry("images", "image")
                table_svc.create_registry("instances", "instance")

            warmup_state["_table_service"] = table_svc
            logger.info(
                "  SQL backend ready (kind=%s, mode=%s)",
                backend.kind, mode or "generic",
            )

            # Assemble ImageService only in appliance mode (image registry
            # already created above). Non-appliance mode skips assembly;
            # the router's _resolve_service then returns 404, matching the
            # design that generic mode does not create image/instance
            # tables. The image module picks up the already-assembled
            # RegistryTableService from _table_service.
            if mode == "appliance":
                from a2x_registry.image.service import ImageService
                from a2x_registry.image.deps import set_image_service
                from a2x_registry.instance.service import InstanceService
                from a2x_registry.instance.deps import set_instance_service

                image_svc = ImageService(table_svc)
                set_image_service(image_svc)
                logger.info("  ImageService assembled (appliance mode)")

                instance_svc = InstanceService(table_svc)
                set_instance_service(instance_svc)
                logger.info("  InstanceService assembled (appliance mode)")

                # Per-node heartbeat assembly (appliance mode only).
                # Creates the node lease store + manager, recovers leases
                # for all nodes that have registered instances, starts the
                # sweeper daemon (wires instance.expire_node as on_expire),
                # and injects the manager into the instance service so
                # _derive_status reflects node liveness. Non-fatal: if this
                # fails, the instance module falls back to all-运行 status
                # (set_heartbeat_service not called -> callback stays None).
                try:
                    from a2x_registry.heartbeat.store import NodeHeartbeatStore
                    from a2x_registry.heartbeat.service import HeartbeatManager
                    from a2x_registry.heartbeat.sweeper import NodeHeartbeatSweeper
                    from a2x_registry.heartbeat.deps import set_node_heartbeat_manager

                    node_store = NodeHeartbeatStore()
                    node_mgr = HeartbeatManager(node_store)
                    recovered_nodes = instance_svc.distinct_nodes()
                    if recovered_nodes:
                        node_mgr.recover_from_persisted(recovered_nodes)
                    set_node_heartbeat_manager(node_mgr)
                    instance_svc.set_heartbeat_service(node_mgr)
                    node_sweeper = NodeHeartbeatSweeper(
                        node_mgr, instance_svc=instance_svc, period=5.0,
                    )
                    node_sweeper.start()
                    warmup_state["_node_heartbeat_sweeper"] = node_sweeper
                    logger.info(
                        "  Per-node heartbeat assembled (recovered %d nodes)",
                        len(recovered_nodes),
                    )
                except Exception as exc:
                    logger.error(
                        "  Per-node heartbeat init failed: %s", exc, exc_info=True,
                    )
                    from a2x_registry.heartbeat.deps import set_node_heartbeat_manager
                    set_node_heartbeat_manager(None)
        except Exception as exc:
            logger.error("  SQL backend init failed: %s", exc, exc_info=True)
            raise

        # 1. Registry
        _stage("Loading registry services...", 5)
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

        # 1d. Cluster (distributed sync) module — opt-in. Loads only when
        # cluster_state.json exists (created by 'a2x-registry cluster init');
        # otherwise stays None and the whole feature is dormant (endpoints
        # 404, read path unchanged). Failure is non-fatal.
        try:
            import os as _os
            from a2x_registry.cluster.store import ClusterStore
            from a2x_registry.cluster.config import ClusterConfig
            from a2x_registry.cluster.deps import set_cluster_store
            from a2x_registry.auth.deps import get_auth_store
            # Advertised base URL peers use to reach us. Defaults empty; set
            # A2X_REGISTRY_CLUSTER_ADVERTISE (e.g. http://10.0.0.2:8000) in a
            # multi-instance deployment so peers can call back.
            advertise = _os.environ.get("A2X_REGISTRY_CLUSTER_ADVERTISE", "")
            cluster_store = ClusterStore.load_or_none(
                config=ClusterConfig.from_env(),   # A2X_REGISTRY_CLUSTER_* overrides
                registry_svc=registry_svc,
                advertise=advertise,
                auth_store_getter=get_auth_store,
            )
            set_cluster_store(cluster_store)
            if cluster_store is not None:
                # Declarative membership control plane. Attaching it lets the
                # node rejoin its persisted cluster + auto-reconnect the mesh
                # (driven by the AntiEntropySweeper's first tick). Absent
                # cluster_id → no peers desired → behaves like today.
                from a2x_registry.cluster.membership import MembershipStore
                cluster_store.membership = MembershipStore(cluster_store)
                # Replicate every local CRUD to peers (default no-op when off).
                registry_svc.set_on_mutation(cluster_store.on_local_mutation)
                # Background daemons (full-mesh): anti-entropy reconcile + GC,
                # and direct-link keepalive/HOLD (the sole liveness path).
                from a2x_registry.cluster.sweepers import (
                    AntiEntropySweeper, KeepaliveMonitor,
                )
                cfg = cluster_store.config
                ae = AntiEntropySweeper(cluster_store, period=cfg.anti_entropy_interval)
                km = KeepaliveMonitor(cluster_store, period=cfg.keepalive_interval)
                ae.start()
                km.start()
                warmup_state["_cluster_anti_entropy"] = ae
                warmup_state["_cluster_keepalive"] = km
                logger.info("  Cluster module loaded (node_id=%s)", cluster_store.node_id)
            else:
                logger.info("  Cluster module not initialized (standalone)")
        except Exception as exc:
            logger.error("  Cluster init failed: %s", exc, exc_info=True)
            from a2x_registry.cluster.deps import set_cluster_store
            set_cluster_store(None)

        # Only wire vector-sync side effects when the [vector] extras are
        # installed; otherwise every SDK register/deregister would spawn a
        # daemon thread that immediately ImportErrors on chromadb.
        if feature_flags.has("vector"):
            registry_svc.set_on_service_changed(
                lambda ds: search_service.schedule_vector_sync(ds)
            )
        logger.info("Warmup: registry done (%.1fs)", time.time() - t0)

        # 2. Taxonomy caches
        _stage("Loading taxonomy tree...", 20)
        from a2x_registry.backend.services.taxonomy_service import get_taxonomy_tree
        for ds in discover_datasets():
            try:
                get_taxonomy_tree(ds)
                logger.info("  Taxonomy cached: %s", ds)
            except Exception as e:
                logger.warning("  Taxonomy failed for %s: %s", ds, e)
        logger.info("Warmup: taxonomy done (%.1fs)", time.time() - t0)

        # 3. A2X engines — pure LLM, runs on the lite install too.
        _stage("Loading A2X search engine...", 35)
        for ds in discover_datasets():
            paths = resolve_dataset_paths(ds)
            if paths["taxonomy_path"].exists():
                for mode in ("get_one", "get_all", "get_important"):
                    _stage(f"Loading A2X {mode} ({ds})...", 35)
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
            _stage("Cleaning vector store...", 58)
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
            _stage("Syncing vector store...", 62)
            for ds in registry_states:
                _stage(f"Syncing vector store ({ds})...", 62)
                try:
                    search_service.sync_vector(ds)
                    logger.info("  Vector sync done: %s", ds)
                except Exception as e:
                    logger.warning("  Vector sync failed for %s: %s", ds, e)
            logger.info("Warmup: vector sync done (%.1fs)", time.time() - t0)

            # 5b. Vector engines
            _stage("Loading vector search engine...", 75)
            for ds in discover_datasets():
                _stage(f"Loading vector engine ({ds})...", 75)
                try:
                    search_service._get_vector(ds)
                    logger.info("  Vector ready: %s", ds)
                except Exception as e:
                    logger.warning("  Vector failed for %s: %s", ds, e)
            logger.info("Warmup: vector done (%.1fs)", time.time() - t0)

            # 6. Pre-heat embedding models (one per unique model across datasets)
            _stage("Pre-heating embedding model...", 90)
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

        warmup_state["stage"] = "complete"
        warmup_state["progress"] = 100
        warmup_state["ready"] = True
        logger.info("Warmup [100%%] complete — total %.1fs", time.time() - t0)

    except Exception as e:
        import traceback
        warmup_state["error"] = str(e)
        warmup_state["ready"] = True
        logger.error("Warmup error: %s\n%s", e, traceback.format_exc())
