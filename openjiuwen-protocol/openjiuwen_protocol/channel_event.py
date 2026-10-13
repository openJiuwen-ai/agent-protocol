# Copyright (c) Huawei Technologies Co., Ltd. 2025-2026. All rights reserved.
"""通道契约：ChannelType 与通道事件。

``ChannelManager`` 及通道实现留在 gateway 仓；本模块仅收纳跨仓消费的枚举与
事件契约。本仓（jiuwenswarm）消费面：``router_client.py`` 的 ``ChannelType``
引用与 ``get_channel`` / ``subscribe_channel_events`` 调用（实例经组装期注入）。
"""

from __future__ import annotations

from enum import Enum


class ChannelType(str, Enum):
    """Channel 类型枚举（值与 gateway 仓实现保持一致，改动需两仓同步）."""

    ACP = "acp"
    WEB = "web"
    FEISHU = "feishu"
    XIAOYI = "xiaoyi"
    DINGTALK = "dingtalk"
    TELEGRAM = "telegram"
    DISCORD = "discord"
    SLACK = "slack"
    WHATSAPP = "whatsapp"
    WECOM = "wecom"
    WECHAT = "wechat"
    SSH = "ssh"
    CLI = "tui"


__all__ = ["ChannelType"]
