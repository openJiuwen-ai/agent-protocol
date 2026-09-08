"""InstanceService — instance management business logic.

双模式编排：

- **模式 A**（向前兼容）：``register_instance`` / ``update_instance`` /
  ``deregister_instance`` / ``list_instances`` —— 纯记录，不碰元戎，
  实例由 gateway（或用户）自行拉起 / 停止后登记。
- **模式 B**（registry 直连元戎）：``create_instance_runtime`` /
  ``delete_instance_runtime`` / ``delete_all_instances`` —— registry 自己调
  元戎沙箱管理接口，**元戎成功后**才增 / 删条目（严格顺序，不做反向补偿：
  元戎已成功但条目写删失败时重试 N 次，仍失败报错记日志，孤儿实例 /
  僵尸条目留日志待对账）。

模式 B 约定（哑转发 + 记录，不持有九问 builtin 规格、不按 agent 类型分支）：

- ``service_id = name = "{user}+{framework}"``（按首个 ``+`` 拆）；
  ``kind`` 由 ``framework == "jiuwenswarm"`` 判；``node``/``address``/
  ``instance_id`` 由元戎回填；``namespace`` 以 registry 配置为准（忽略入参）。
- 同 ``service_id`` 在途操作互斥：in-flight 锁 + 获取等待上限，超时抛
  ``InstanceInProgressError``（→ 409）。
- 创建为异步语义：create 返回后等元戎探针通过（status=running，可配、
  0 关闭）并取齐落点后才写条目回响应。
- 幂等：条目已存在且 status=运行 → 直接回现有条目不二次拉起；
  条目存在但非运行 → 重新拉起并覆盖（旧元戎实例留日志待对账）。
- DELETE ALL：对每个条目并发（限流）调元戎，逐条报结果、部分失败不回滚。

Persistence goes through ``RegistryTableService`` (SQL backend); this
service does not hold a backend/store directly. The ``data`` JSON column
holds ``{address, instance_id, image_name, created_at, last_active_at,
status}`` — these are not promoted columns, so ``update_instance`` must
merge ``address`` / ``instance_id`` / ``status`` into the existing ``data``
dict before patching.

``status`` (运行 / 停止 / 异常) is persisted inside ``data`` and written
by the gateway via PATCH (元戎 List): the registry does NOT receive
heartbeats, derive status, or auto-evict — it only records what the
gateway reports.
"""

from __future__ import annotations

import asyncio
import logging
from typing import Any, Dict, List, Optional, Tuple

from a2x_registry.common.ids import now_iso
from a2x_registry.register.table_repo import TableRepo

from a2x_registry.instance import constants as consts
from a2x_registry.instance.yuanrong_client import (
    YuanrongAgentApiError,
    YuanrongAgentTimeoutError,
    YuanrongSandboxClient,
)

from .errors import (
    InstanceInProgressError,
    InstanceNotFoundError,
    InstanceStoreError,
    InstanceValidationError,
    RuntimeNotConfiguredError,
    YuanrongFailedError,
    YuanrongTimeoutError,
)

logger = logging.getLogger(__name__)

INSTANCE_REGISTRY = "instances"


class InstanceService:
    """Instance management business layer（模式 A 记录 + 模式 B 元戎编排）."""

    __slots__ = ("_table_svc", "_yuanrong", "_inflight_locks")

    def __init__(
        self,
        table_svc: TableRepo,
        yuanrong_client: Optional[YuanrongSandboxClient] = None,
    ) -> None:
        self._table_svc = table_svc
        self._yuanrong = yuanrong_client
        # 同 service_id 在途操作互斥锁（进程内；单节点部署下正确，
        # 多机化时需换分布式锁）。锁在首个事件循环内惰性创建并复用。
        self._inflight_locks: Dict[str, asyncio.Lock] = {}

    # ------------------------------------------------------------------
    # 模式 A：register_instance
    # ------------------------------------------------------------------

    def register_instance(self, entry: Dict[str, Any]) -> Dict[str, Any]:
        self._validate_entry(entry)

        sid = entry["service_id"]
        now = now_iso()

        existing = self._table_svc.query(
            INSTANCE_REGISTRY, {"service_id": sid}
        )
        if existing:
            old_data = existing[0].get("data", {}) or {}
            created_at = old_data.get("created_at", now)
        else:
            created_at = now

        # instance_id is the 元戎 instance ID (optional, never a key);
        # status starts at 运行 (registered = launched, per OpenAPI).
        db_entry = {
            "service_id": sid,
            "kind": entry["kind"],
            "framework": entry["framework"],
            "framework_version": entry["framework_version"],
            "node": entry["node"],
            "user": entry["user"],
            "data": {
                "address": entry["address"],
                "instance_id": entry.get("instance_id") or "",
                "image_name": entry.get("image_name") or "",
                "created_at": created_at,
                "last_active_at": now,
                "status": consts.STATUS_RUNNING,
            },
        }
        stored = self._table_svc.register(INSTANCE_REGISTRY, db_entry)
        logger.info("register_instance %s (node=%s)", sid, entry["node"])
        return self._to_entry(stored)

    # ------------------------------------------------------------------
    # 模式 A：update_instance
    # ------------------------------------------------------------------

    def update_instance(
        self, service_id: str, fields: Dict[str, Any]
    ) -> Dict[str, Any]:
        has_node = fields.get("node") is not None
        has_address = fields.get("address") is not None
        has_instance_id = fields.get("instance_id") is not None
        has_status = fields.get("status") is not None
        if not (has_node or has_address or has_instance_id or has_status):
            raise InstanceValidationError(
                "at least one of node/address/instance_id/status "
                "must be provided"
            )
        if has_status and fields["status"] not in consts.VALID_STATUSES:
            raise InstanceValidationError(
                f"invalid status: {fields['status']!r}, "
                f"must be one of {consts.VALID_STATUSES}"
            )

        existing = self._table_svc.query(
            INSTANCE_REGISTRY, {"service_id": service_id}
        )
        if not existing:
            raise InstanceNotFoundError(
                f"instance '{service_id}' not found"
            )

        row = existing[0]
        data = dict(row.get("data", {}) or {})

        patch_fields: Dict[str, Any] = {}
        if has_node:
            patch_fields["node"] = fields["node"]
        if has_instance_id:
            data["instance_id"] = fields["instance_id"]
        if has_status:
            data["status"] = fields["status"]
        if has_address:
            data["address"] = fields["address"]
            data["last_active_at"] = now_iso()
        if has_instance_id or has_status or has_address:
            patch_fields["data"] = data

        updated = self._table_svc.patch(INSTANCE_REGISTRY, service_id, patch_fields)
        logger.info("update_instance %s (fields=%s)", service_id, sorted(patch_fields))
        return self._to_entry(updated)

    # ------------------------------------------------------------------
    # 模式 A：deregister_instance（仅删条目，幂等）
    # ------------------------------------------------------------------

    def deregister_instance(self, service_id: str) -> Dict[str, Any]:
        deleted = self._table_svc.deregister(INSTANCE_REGISTRY, service_id)
        logger.info(
            "deregister_instance %s (deleted=%s)", service_id, deleted
        )
        return {"service_id": service_id, "deleted": deleted}

    # ------------------------------------------------------------------
    # 模式 B：create_instance_runtime（registry 调元戎创建）
    # ------------------------------------------------------------------

    async def create_instance_runtime(self, payload: Dict[str, Any]) -> Dict[str, Any]:
        """模式 B 创建：元戎 create → 等 running → 取落点 → 写条目。

        严格顺序：元戎成功后才写条目；条目写失败重试，仍失败抛
        ``InstanceStoreError``（元戎实例留日志待对账，不反向补偿删除）。
        """
        name = str(payload.get("name") or "")
        version = str(payload.get("version") or "")
        workspace = str(payload.get("workspace") or "")
        if not name:
            raise InstanceValidationError("missing required field: name")
        if not version:
            raise InstanceValidationError("missing required field: version")
        if not workspace:
            raise InstanceValidationError("missing required field: workspace")
        if not workspace.startswith("/"):
            raise InstanceValidationError(
                f"workspace must be an absolute path: {workspace!r}"
            )
        user, framework = self._split_service_id(name)
        self._validate_runtime_spec_shape(payload.get("runtime_spec"))

        client = self._require_yuanrong()

        service_id = name
        kind = "九问" if framework == consts.BUILTIN_AGENT_FRAMEWORK else "三方"
        image_name = str(payload.get("image_name") or "")
        runtime_spec = payload.get("runtime_spec")

        await self._acquire_lock(service_id)
        try:
            # 幂等：已存在且运行 → 直接回现有条目，不二次拉起
            existing = self._table_svc.query(
                INSTANCE_REGISTRY, {"service_id": service_id}
            )
            if existing:
                entry = self._to_entry(existing[0])
                if entry["status"] == consts.STATUS_RUNNING:
                    logger.info(
                        "create_instance_runtime %s idempotent-hit "
                        "(instance_id=%s)", service_id, entry["instance_id"],
                    )
                    return entry
                old_iid = entry.get("instance_id") or ""
                if old_iid:
                    logger.warning(
                        "create_instance_runtime %s recreating while old "
                        "runtime instance %s may remain (reconcile needed)",
                        service_id, old_iid,
                    )

            try:
                info = await client.create_sandbox(
                    namespace=client.agent_namespace,
                    name=name,
                    workspace=workspace,
                    runtime_spec=runtime_spec,
                    env_vars=payload.get("env_vars"),
                    mounts=payload.get("mounts"),
                )
            except ValueError as exc:
                raise InstanceValidationError(str(exc))
            except YuanrongAgentTimeoutError as exc:
                raise YuanrongTimeoutError(str(exc))
            except YuanrongAgentApiError as exc:
                raise YuanrongFailedError(str(exc))

            instance_id = info.sandbox_id

            # 等元戎探针通过（wait_running_timeout_s > 0 时）；否则直接取落点
            try:
                if getattr(client, "wait_running_timeout_s", 0) > 0:
                    instance = await client.wait_until_running(instance_id)
                else:
                    instance = await client.get_agent_info(instance_id)
            except YuanrongAgentTimeoutError as exc:
                raise YuanrongTimeoutError(str(exc))
            except YuanrongAgentApiError as exc:
                raise YuanrongFailedError(str(exc))

            node = str(instance.get("node_ip") or "")
            sandbox_ip = str(instance.get("sandbox_ip") or "")
            port = instance.get("port")
            if sandbox_ip and port:
                address = f"{sandbox_ip}:{port}"
            else:
                address = sandbox_ip

            entry = {
                "service_id": service_id,
                "kind": kind,
                "framework": framework,
                "framework_version": version,
                "node": node,
                "user": user,
                "data": {
                    "address": address,
                    "instance_id": instance_id,
                    "image_name": image_name,
                },
            }
            stored = self._register_with_retry(entry, service_id)
            logger.info(
                "create_instance_runtime %s (instance_id=%s, node=%s)",
                service_id, instance_id, node,
            )
            return self._to_entry(stored)
        finally:
            self._lock_for(service_id).release()

    # ------------------------------------------------------------------
    # 模式 B：delete_instance_runtime（registry 调元戎删除）
    # ------------------------------------------------------------------

    async def delete_instance_runtime(self, service_id: str) -> Dict[str, Any]:
        """模式 B 删除：先按条目 instance_id 调元戎，成功后删条目。

        - 条目不存在 → ``deleted=False``（幂等，不调元戎）
        - 条目无 instance_id（模式 A 登记未回填）→ 仅删条目，
          ``runtime_deleted=False``
        - 元戎已无该实例（404，由客户端归一为成功）→ 视作已删继续删条目
        - 元戎失败 / 超时 → 抛错，条目保留
        """
        client = self._require_yuanrong()

        await self._acquire_lock(service_id)
        try:
            return await self._delete_locked(client, service_id)
        finally:
            self._lock_for(service_id).release()

    async def _delete_locked(
        self, client: YuanrongSandboxClient, service_id: str
    ) -> Dict[str, Any]:
        """删除单个条目（调用方已持有 in-flight 锁）。"""
        existing = self._table_svc.query(
            INSTANCE_REGISTRY, {"service_id": service_id}
        )
        if not existing:
            return {
                "service_id": service_id,
                "deleted": False,
                "runtime_deleted": False,
            }

        data = existing[0].get("data", {}) or {}
        instance_id = str(data.get("instance_id") or "")
        if not instance_id:
            deleted = self._deregister_with_retry(service_id)
            return {
                "service_id": service_id,
                "deleted": deleted,
                "runtime_deleted": False,
            }

        try:
            await client.delete_sandbox(instance_id)
        except YuanrongAgentTimeoutError as exc:
            raise YuanrongTimeoutError(str(exc))
        except YuanrongAgentApiError as exc:
            raise YuanrongFailedError(str(exc))

        deleted = self._deregister_with_retry(service_id)
        logger.info(
            "delete_instance_runtime %s (instance_id=%s, deleted=%s)",
            service_id, instance_id, deleted,
        )
        return {
            "service_id": service_id,
            "deleted": deleted,
            "runtime_deleted": True,
        }

    # ------------------------------------------------------------------
    # 模式 B：delete_all_instances（ALL 批量，逐条报结果、部分失败不回滚）
    # ------------------------------------------------------------------

    async def delete_all_instances(self, with_runtime: bool) -> Dict[str, Any]:
        """ALL 批量删除：逐条抢 in-flight 锁（在途记 error，不整体 409）。"""
        rows = self._table_svc.query(INSTANCE_REGISTRY)
        semaphore = asyncio.Semaphore(consts.ALL_CONCURRENCY)

        async def _one(row: Dict[str, Any]) -> Dict[str, Any]:
            sid = row.get("service_id", "")
            async with semaphore:
                try:
                    await self._acquire_lock(sid)
                except InstanceInProgressError:
                    # 批量不整体 409：在途条目逐条记 error
                    return {
                        "service_id": sid, "deleted": False,
                        "error": "in_progress",
                    }
                try:
                    if with_runtime:
                        client = self._require_yuanrong()
                        return await self._delete_locked(client, sid)
                    return {
                        "service_id": sid,
                        "deleted": self._deregister_with_retry(sid),
                    }
                except (
                    YuanrongFailedError,
                    YuanrongTimeoutError,
                    RuntimeNotConfiguredError,
                    InstanceStoreError,
                ) as exc:
                    return {
                        "service_id": sid, "deleted": False,
                        "error": str(exc),
                    }
                finally:
                    self._lock_for(sid).release()

        results = await asyncio.gather(*(_one(r) for r in rows))
        deleted = sum(1 for r in results if r.get("deleted"))
        logger.info(
            "delete_all_instances (with_runtime=%s, total=%d, deleted=%d)",
            with_runtime, len(rows), deleted,
        )
        return {"total": len(rows), "deleted": deleted, "results": list(results)}

    # ------------------------------------------------------------------
    # 模式 A：list_instances (paginated + SQL push-down)
    # ------------------------------------------------------------------

    def list_instances(
        self,
        filter: Optional[Dict[str, Any]] = None,
        include_unhealthy: bool = False,
        size: int = -1,
        page: int = 1,
    ) -> Tuple[List[Dict[str, Any]], int]:
        """Query instances with optional filters, pagination, and status.

        When ``include_unhealthy=False`` (default), only rows whose
        persisted ``data.status`` equals 运行 are returned — pushed down
        via ``only_status`` so ``LIMIT/OFFSET`` and ``X-Total-Count`` stay
        correct across backends (legacy rows without a stored status
        default to 运行).

        Returns ``(entries, total)`` — total is the filtered count before
        pagination.
        """
        only_status: Optional[str] = (
            None if include_unhealthy else consts.STATUS_RUNNING
        )

        offset = max(0, (page - 1) * size) if size > 0 else 0
        rows, total = self._table_svc.query_paginated(
            INSTANCE_REGISTRY,
            query_filter=filter or None,
            only_status=only_status,
            order_by=_INSTANCE_ORDER,
            limit=size if size > 0 else -1,
            offset=offset,
        )
        entries = [self._to_entry(r) for r in rows]
        return entries, total

    # ------------------------------------------------------------------
    # internal helpers
    # ------------------------------------------------------------------

    def _require_yuanrong(self) -> YuanrongSandboxClient:
        if self._yuanrong is None:
            raise RuntimeNotConfiguredError(
                "未配置元戎 frontend_endpoint"
                f"（{consts.YUANRONG_ENDPOINT_ENV}）"
            )
        return self._yuanrong

    def _lock_for(self, service_id: str) -> asyncio.Lock:
        return self._inflight_locks.setdefault(service_id, asyncio.Lock())

    async def _acquire_lock(self, service_id: str) -> None:
        lock = self._lock_for(service_id)
        try:
            await asyncio.wait_for(lock.acquire(), consts.LOCK_WAIT_S)
        except asyncio.TimeoutError:
            raise InstanceInProgressError(
                f"service_id {service_id} 正在创建 / 删除"
            )

    @staticmethod
    def _split_service_id(name: str) -> Tuple[str, str]:
        """按首个 ``+`` 拆 ``user`` / ``framework``（user 侧不得为空）。"""
        user, _, framework = name.partition("+")
        if not user.strip() or not framework.strip():
            raise InstanceValidationError(
                f"name must be '{{user}}+{{framework}}' with both sides "
                f"non-empty, got: {name!r}"
            )
        return user, framework

    @staticmethod
    def _validate_runtime_spec_shape(runtime_spec: Any) -> None:
        """锁前快速结构校验（完整规范化由元戎客户端完成）。"""
        if not isinstance(runtime_spec, dict):
            raise InstanceValidationError("runtime_spec must be an object")
        if not str(runtime_spec.get("runtime") or "").strip():
            raise InstanceValidationError(
                "runtime_spec.runtime is required to create sandbox"
            )
        rootfs = runtime_spec.get("rootfs")
        if not isinstance(rootfs, dict) or not str(
            rootfs.get("imageurl") or ""
        ).strip():
            raise InstanceValidationError(
                "runtime_spec.rootfs.imageurl is required to create sandbox"
            )

    def _register_with_retry(
        self, entry: Dict[str, Any], service_id: str
    ) -> Dict[str, Any]:
        """元戎成功后的条目写入（保留既有 created_at），失败重试。"""
        now = now_iso()
        existing = self._table_svc.query(
            INSTANCE_REGISTRY, {"service_id": service_id}
        )
        if existing:
            created_at = (existing[0].get("data", {}) or {}).get("created_at", now)
        else:
            created_at = now

        db_entry = dict(entry)
        db_entry["data"] = {
            **entry["data"],
            "created_at": created_at,
            "last_active_at": now,
            "status": consts.STATUS_RUNNING,
        }

        last_exc: Optional[BaseException] = None
        for attempt in range(1, consts.ENTRY_WRITE_RETRIES + 1):
            try:
                return self._table_svc.register(INSTANCE_REGISTRY, db_entry)
            except Exception as exc:  # noqa: BLE001 - 数据层异常统一重试
                last_exc = exc
                logger.error(
                    "entry write failed for %s (attempt %d/%d): %s",
                    service_id, attempt, consts.ENTRY_WRITE_RETRIES, exc,
                )
        raise InstanceStoreError(
            f"entry write failed after {consts.ENTRY_WRITE_RETRIES} retries "
            f"for {service_id} (runtime instance may be orphaned, "
            f"reconcile needed): {last_exc}"
        )

    def _deregister_with_retry(self, service_id: str) -> bool:
        """元戎成功后的条目删除，失败重试。"""
        last_exc: Optional[BaseException] = None
        for attempt in range(1, consts.ENTRY_WRITE_RETRIES + 1):
            try:
                return self._table_svc.deregister(INSTANCE_REGISTRY, service_id)
            except Exception as exc:  # noqa: BLE001 - 数据层异常统一重试
                last_exc = exc
                logger.error(
                    "entry delete failed for %s (attempt %d/%d): %s",
                    service_id, attempt, consts.ENTRY_WRITE_RETRIES, exc,
                )
        raise InstanceStoreError(
            f"entry delete failed after {consts.ENTRY_WRITE_RETRIES} retries "
            f"for {service_id} (zombie entry remains, reconcile needed): "
            f"{last_exc}"
        )

    @staticmethod
    def _validate_entry(entry: Dict[str, Any]) -> None:
        for field in _REQUIRED_FIELDS:
            val = entry.get(field)
            if val is None or val == "":
                raise InstanceValidationError(
                    f"missing required field: {field}"
                )
        if entry["kind"] not in consts.VALID_KINDS:
            raise InstanceValidationError(
                f"invalid kind: {entry['kind']!r}, "
                f"must be one of {consts.VALID_KINDS}"
            )

    def _to_entry(self, row: Dict[str, Any]) -> Dict[str, Any]:
        data = row.get("data", {}) or {}
        node = row.get("node", "")
        return {
            "service_id": row["service_id"],
            "kind": row["kind"],
            "framework": row["framework"],
            "framework_version": row["framework_version"],
            "node": node,
            "address": data.get("address", ""),
            "instance_id": data.get("instance_id", "") or "",
            "image_name": data.get("image_name", "") or "",
            "user": row["user"],
            "created_at": data.get("created_at", ""),
            "last_active_at": data.get("last_active_at", ""),
            "status": data.get("status") or consts.STATUS_RUNNING,
        }


# Required fields for register_instance (mode A).
_REQUIRED_FIELDS = (
    "service_id", "kind", "framework", "framework_version",
    "node", "address", "user",
)

# Deterministic sort order for instance listing.
_INSTANCE_ORDER = (
    "framework asc",
    "user asc",
    "service_id asc"
)
