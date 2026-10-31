# Copyright (c) Huawei Technologies Co., Ltd. 2025-2026. All rights reserved.
"""Gateway Redis key / Pub-Sub 频道命名契约。

来源：``jiuwenswarm/extensions/redis/redis_keys.py``。相对名（不含
``RedisConfig.key_prefix``）是 gateway 与 AgentServer 的跨进程契约：一侧写、
另一侧读，字面量必须逐字一致。

边界：本模块只固化**模板字面量**；拼装/校验函数（空值检查、prefix 归一化）
属实现逻辑，留在各仓实现侧。
"""

from __future__ import annotations

#: ``key_prefix`` 与相对名之间的分隔符。
REDIS_KEY_PREFIX_SEPARATOR: str = ":"

#: ``{prefix}gateway:session_map:{identity_key}`` Hash。
GATEWAY_SESSION_MAP_HASH_TEMPLATE: str = "gateway:session_map:{identity_key}"
#: ``{prefix}gateway:cron_jobs:{gateway_instance_id}`` Hash。
GATEWAY_CRON_JOBS_HASH_TEMPLATE: str = "gateway:cron_jobs:{gateway_instance_id}"
#: ``{prefix}gateway:leader:{channel_id}`` String（带 TTL）。
GATEWAY_LEADER_LOCK_TEMPLATE: str = "gateway:leader:{channel_id}"
#: ``{prefix}gateway:cron_live:{session_id}`` Pub/Sub 频道。
GATEWAY_CRON_LIVE_CHANNEL_TEMPLATE: str = "gateway:cron_live:{session_id}"
#: ``{prefix}gateway:cron_live_replay:{session_id}`` List（短 TTL 晚订回放）。
GATEWAY_CRON_LIVE_REPLAY_TEMPLATE: str = "gateway:cron_live_replay:{session_id}"
#: ``{prefix}gateway:cron_permission_hold:{job_id}`` String（TTL=审批窗口）。
GATEWAY_CRON_PERMISSION_HOLD_TEMPLATE: str = "gateway:cron_permission_hold:{job_id}"
#: ``{prefix}gateway:cron_permission_hold_by_session:{session_id}`` → job_id。
GATEWAY_CRON_PERMISSION_HOLD_BY_SESSION_TEMPLATE: str = (
    "gateway:cron_permission_hold_by_session:{session_id}"
)

__all__ = [
    "GATEWAY_CRON_JOBS_HASH_TEMPLATE",
    "GATEWAY_CRON_LIVE_CHANNEL_TEMPLATE",
    "GATEWAY_CRON_LIVE_REPLAY_TEMPLATE",
    "GATEWAY_CRON_PERMISSION_HOLD_BY_SESSION_TEMPLATE",
    "GATEWAY_CRON_PERMISSION_HOLD_TEMPLATE",
    "GATEWAY_LEADER_LOCK_TEMPLATE",
    "GATEWAY_SESSION_MAP_HASH_TEMPLATE",
    "REDIS_KEY_PREFIX_SEPARATOR",
]
