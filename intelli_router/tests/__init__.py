"""
SDK LLM Tests - 测试模块
"""
# 使用延迟导入避免循环依赖
__all__ = [
    "TestAPIPooling",
    "TestRoutingStrategies",
    "TestStateManagement",
    "TestCaching",
    "TestConcurrentRequests"
]


def __getattr__(name: str):
    """延迟导入"""
    if name in __all__:
        from . import sdk_proxy_demo
        return getattr(sdk_proxy_demo, name)
    raise AttributeError(f"module {__name__!r} has no attribute {name!r}")
