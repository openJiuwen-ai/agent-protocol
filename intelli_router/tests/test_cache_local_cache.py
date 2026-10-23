"""Tests for intelli_router.cache.local_cache."""
import time
import threading
import pytest
from intelli_router.cache.local_cache import CacheEntry, LocalCache


# -------- CacheEntry --------

def test_cache_entry_is_expired():
    entry = CacheEntry(value="v", ttl=0.1, created_at=time.time() - 1)
    assert entry.is_expired(time.time()) is True


def test_cache_entry_not_expired():
    entry = CacheEntry(value="v", ttl=3600, created_at=time.time())
    assert entry.is_expired(time.time()) is False


# -------- LocalCache --------

def test_set_and_get(empty_cache):
    empty_cache.set_cache("key", "value")
    assert empty_cache.get_cache("key") == "value"


def test_get_default(empty_cache):
    assert empty_cache.get_cache("missing", default=42) == 42


def test_delete_existing(empty_cache):
    empty_cache.set_cache("key", "value")
    assert empty_cache.delete_cache("key") is True
    assert empty_cache.get_cache("key") is None


def test_delete_missing(empty_cache):
    assert empty_cache.delete_cache("nonexistent") is False


def test_clear(empty_cache):
    empty_cache.set_cache("a", 1)
    empty_cache.set_cache("b", 2)
    empty_cache.clear_cache()
    assert empty_cache.size() == 0


def test_size(empty_cache):
    assert empty_cache.size() == 0
    empty_cache.set_cache("a", 1)
    assert empty_cache.size() == 1
    empty_cache.set_cache("b", 2)
    assert empty_cache.size() == 2


def test_keys(empty_cache):
    empty_cache.set_cache("a", 1)
    empty_cache.set_cache("b", 2)
    keys = empty_cache.keys()
    assert "a" in keys
    assert "b" in keys


def test_lru_eviction(small_cache):
    """max_size=3, insert 4 keys -> oldest evicted."""
    small_cache.set_cache("a", 1)
    small_cache.set_cache("b", 2)
    small_cache.set_cache("c", 3)
    small_cache.set_cache("d", 4)
    assert small_cache.size() == 3
    assert small_cache.get_cache("a") is None  # evicted
    assert small_cache.get_cache("b") == 2
    assert small_cache.get_cache("c") == 3
    assert small_cache.get_cache("d") == 4


def test_lru_reorder_on_get(small_cache):
    """Accessing key 'a' moves it to recently used, 'b' gets evicted."""
    small_cache.set_cache("a", 1)
    small_cache.set_cache("b", 2)
    small_cache.set_cache("c", 3)
    # Access 'a' -> becomes recently used
    small_cache.get_cache("a")
    small_cache.set_cache("d", 4)  # should evict 'b' (LRU)
    assert small_cache.get_cache("a") == 1
    assert small_cache.get_cache("b") is None  # evicted
    assert small_cache.get_cache("c") == 3
    assert small_cache.get_cache("d") == 4


def test_set_existing_key_no_eviction(small_cache):
    """Overwriting existing key should not trigger eviction."""
    small_cache.set_cache("a", 1)
    small_cache.set_cache("b", 2)
    small_cache.set_cache("c", 3)
    small_cache.set_cache("a", 99)  # overwrite, not new
    assert small_cache.size() == 3
    assert small_cache.get_cache("a") == 99


def test_ttl_expiry(short_ttl_cache):
    short_ttl_cache.set_cache("key", "value")
    time.sleep(0.02)  # wait past TTL
    assert short_ttl_cache.get_cache("key") is None


def test_get_cache_with_ttl_valid(empty_cache):
    empty_cache.set_cache("key", "value", ttl=3600)
    val, ttl = empty_cache.get_cache_with_ttl("key")
    assert val == "value"
    assert ttl is not None
    assert ttl > 0


def test_get_cache_with_ttl_missing(empty_cache):
    val, ttl = empty_cache.get_cache_with_ttl("nonexistent")
    assert val is None
    assert ttl is None


def test_get_cache_with_ttl_expired(short_ttl_cache):
    short_ttl_cache.set_cache("key", "value")
    time.sleep(0.02)
    val, ttl = short_ttl_cache.get_cache_with_ttl("key")
    assert val is None
    assert ttl is None


def test_cleanup_expired(empty_cache):
    empty_cache.set_cache("expired_key", "v", ttl=0.001)
    empty_cache.set_cache("good_key", "v", ttl=3600)
    time.sleep(0.01)
    cleaned = empty_cache.cleanup_expired()
    assert cleaned >= 1
    assert empty_cache.get_cache("good_key") == "v"


def test_cleanup_expired_none(empty_cache):
    empty_cache.set_cache("a", 1, ttl=3600)
    assert empty_cache.cleanup_expired() == 0


@pytest.mark.asyncio
async def test_async_set_and_get(empty_cache):
    await empty_cache.async_set_cache("async_key", "async_val")
    val = await empty_cache.async_get_cache("async_key")
    assert val == "async_val"


@pytest.mark.asyncio
async def test_async_delete(empty_cache):
    await empty_cache.async_set_cache("del_key", "val")
    assert await empty_cache.async_delete_cache("del_key") is True
    assert await empty_cache.async_get_cache("del_key") is None
    assert await empty_cache.async_delete_cache("nonexistent") is False


def test_thread_safety():
    """Concurrent writes should not corrupt cache."""
    cache = LocalCache(max_size=100)
    errors = []

    def worker(key_prefix):
        try:
            for i in range(100):
                cache.set_cache(f"{key_prefix}_{i}", i)
                cache.get_cache(f"{key_prefix}_{i}")
        except Exception as e:
            errors.append(e)

    threads = [threading.Thread(target=worker, args=(f"t{t}",)) for t in range(10)]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    assert len(errors) == 0
    assert cache.size() > 0
