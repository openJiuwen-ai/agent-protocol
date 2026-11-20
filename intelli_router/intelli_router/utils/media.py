"""多模态内容处理工具函数。

提供 data URI 解析等功能，供各 provider adapter 复用。
"""

import logging

logger = logging.getLogger(__name__)


def is_data_uri(s: str) -> bool:
    """判断字符串是否为 data URI。"""
    return isinstance(s, str) and s.startswith("data:")


def parse_data_uri(uri: str) -> tuple[str, str]:
    """解析 data URI 为 (mime_type, base64_data)。

    Args:
        uri: data URI 字符串，如 "data:image/png;base64,iVBOR..."

    Returns:
        (mime_type, base64_data) 元组

    Raises:
        ValueError: URI 格式不合法
    """
    if not uri.startswith("data:"):
        raise ValueError(f"Not a data URI: {uri[:50]}")

    # data:image/png;base64,iVBOR...
    try:
        header, data = uri.split(",", 1)
    except ValueError:
        raise ValueError(f"Invalid data URI (no comma): {uri[:50]}")

    # header = "data:image/png;base64"
    meta = header[5:]  # strip "data:"
    parts = meta.split(";")
    mime_type = parts[0] if parts[0] else "application/octet-stream"

    return mime_type, data
