# Copyright (c) Huawei Technologies Co., Ltd. 2025-2026. All rights reserved.
"""日志级别配置的请求体字段契约（``logging`` 段）。

来源：``gateway/config/logging/repository.py`` 与 ``jiuwenswarm/common/utils.py``。
字段名同时被 common 层（``apply_logging_config_payload`` / ``_logging_config_row_to_dict``）
与 gateway 层（repository / access / 管理面 receiver）使用；因分层规则禁止 common
import gateway，字段清单收口到本包作为唯一中立来源。

边界：本模块只固化**字段名与操作字面量**；``update_log_levels`` 等热更新行为、
DB 行到 payload 的映射函数均属实现逻辑，留在各仓实现侧。
"""

from __future__ import annotations

#: 配置文件中的日志段名（``config.yaml`` 的 ``logging`` 键）。
LOGGING_CONFIG_SECTION: str = "logging"

#: 日志级别字段（顺序即管理面/前端展示顺序）。
LOGGING_LEVEL_FIELDS: tuple[str, ...] = (
    "level",
    "console_level",
    "gateway",
    "channel",
    "agent_server",
    "full",
)

#: payload 中的操作字段名。
LOG_CONFIG_OP_FIELD: str = "op"
#: 删除日志配置、恢复代码默认级别。
LOG_CONFIG_OP_DELETE: str = "delete"

__all__ = [
    "LOGGING_CONFIG_SECTION",
    "LOGGING_LEVEL_FIELDS",
    "LOG_CONFIG_OP_DELETE",
    "LOG_CONFIG_OP_FIELD",
]
