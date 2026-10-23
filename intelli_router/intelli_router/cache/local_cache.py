"""
SDK LLM Cache - 本地缓存

LocalCache: 纯内存缓存，线程安全，LRU策略
"""
from typing import Dict, Any, Optional, Tuple
from dataclasses import dataclass, field
import time
from threading import Lock
from collections import OrderedDict
from ..utils.exceptions import CacheError

# 最大缓存条目数
DEFAULT_MAX_CACHE_SIZE = 1000
# 默认TTL (秒)
DEFAULT_TTL = 3600

@dataclass
class CacheEntry:
    """缓存条目"""
    value: Any
    ttl: float
    created_at: float = field(default_factory=time.time)

    def is_expired(self, now: float) -> bool:
        """检查是否过期"""
        return now - self.created_at > self.ttl

class LocalCache:
    """
    本地内存缓存 - 线程安全，LRU策略

    线程安全: 使用Lock保护所有操作
    LRU: 最近使用移至末尾
    OrderedDict: 保时序
    """

    def __init__(
        self,
        max_size: int = DEFAULT_MAX_CACHE_SIZE,
        default_ttl: float = DEFAULT_TTL
    ):
        self._cache: OrderedDict[str, CacheEntry] = OrderedDict()
        self._lock = Lock()
        self.max_size = max_size
        self.default_ttl = default_ttl

    def set_cache(
        self,
        key: str,
        value: Any,
        ttl: Optional[float] = None,
        **kwargs
    ) -> None:
        """设置缓存"""
        with self._lock:
            # 如果超过最大大小，移除最旧的
            if len(self._cache) >= self.max_size and key not in self._cache:
                self._cache.popitem(last=False)

            entry = CacheEntry(
                value=value,
                ttl=ttl or self.default_ttl
            )
            self._cache[key] = entry
            # 移至末尾 (最近使用)
            self._cache.move_to_end(key)

    def get_cache(
        self,
        key: str,
        default: Any = None,
        **kwargs
    ) -> Any:
        """获取缓存"""
        with self._lock:
            entry = self._cache.get(key)
            if entry is None:
                return default
            # 检查过期
            if entry.is_expired(time.time()):
                del self._cache[key]
                return default
            # 移至末尾 (最近使用)
            self._cache.move_to_end(key)
            return entry.value

    def delete_cache(self, key: str) -> bool:
        """删除缓存"""
        with self._lock:
            if key in self._cache:
                del self._cache[key]
                return True
            return False

    def clear_cache(self) -> None:
        """清空缓存"""
        with self._lock:
            self._cache.clear()

    def get_cache_with_ttl(self, key: str) -> Tuple[Optional[Any], Optional[float]]:
        """获取缓存并返回剩余TTL"""
        with self._lock:
            entry = self._cache.get(key)
            if entry is None:
                return None, None
            now = time.time()
            if entry.is_expired(now):
                del self._cache[key]
                return None, None
            remaining_ttl = entry.ttl - (now - entry.created_at)
            self._cache.move_to_end(key)
            return entry.value, remaining_ttl

    def keys(self) -> list:
        """获取所有键"""
        with self._lock:
            return list(self._cache.keys())

    def size(self) -> int:
        """获取缓存大小"""
        with self._lock:
            return len(self._cache)

    def cleanup_expired(self) -> int:
        """清理过期条目，返回清理数量"""
        now = time.time()
        expired_keys = []
        with self._lock:
            for key, entry in self._cache.items():
                if entry.is_expired(now):
                    expired_keys.append(key)
            for key in expired_keys:
                del self._cache[key]
        return len(expired_keys)

    # ============ 异步方法 ============

    async def async_set_cache(
        self,
        key: str,
        value: Any,
        ttl: Optional[float] = None,
        **kwargs
    ) -> None:
        """异步设置缓存 (实际同步执行)"""
        self.set_cache(key, value, ttl, **kwargs)

    async def async_get_cache(
        self,
        key: str,
        default: Any = None,
        **kwargs
    ) -> Any:
        """异步获取缓存"""
        return self.get_cache(key, default, **kwargs)

    async def async_delete_cache(self, key: str) -> bool:
        """异步删除缓存"""
        return self.delete_cache(key)
