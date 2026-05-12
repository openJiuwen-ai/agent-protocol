"""
SDK LLM 异常定义

分层异常体系:
- SdkLlmError (基桩)
- RouterError (路由)
- DeploymentError (部署)
- CacheError (缓存)
- HealthCheckError (健查)
- ConfigError (配置)
"""
from typing import Optional, Dict, Any

class SdkLlmError(Exception):
    """SDK LLM 基础异常"""

    def __init__(self, message: str, details: Optional[Dict[str, Any]] = None):
        self.message = message
        self.details = details or {}
        super().__init__(message)

    def to_dict(self) -> Dict[str, Any]:
        """转换为字典 (用于日志/响应)"""
        return {
            "error_type": self.__class__.__name__,
            "message": self.message,
            "details": self.details
        }

# ============ 路由异常 ============

class RouterError(SdkLlmError):
    """路由器基础异常"""
    pass

class NoDeploymentAvailable(RouterError):
    """无可用部署"""

    def __init__(self, model: str, reason: str = ""):
        super().__init__(
            message=f"No available deployment for model '{model}'. {reason}",
            details={"model": model, "reason": reason}
        )

class AllDeploymentsFailed(RouterError):
    """所有部署失败"""

    def __init__(self, model: str, errors: list):
        super().__init__(
            message=f"All deployments failed for model '{model}'",
            details={"model": model, "errors": errors}
        )

class StrategyError(RouterError):
    """策略错误"""

    def __init__(self, strategy_name: str, reason: str):
        super().__init__(
            message=f"Strategy '{strategy_name}' error: {reason}",
            details={"strategy": strategy_name, "reason": reason}
        )

# ============ 部署异常 ============

class DeploymentError(SdkLlmError):
    """部署基础异常"""
    pass

class DeploymentInCooldown(DeploymentError):
    """部署在冷却期"""

    def __init__(self, deployment_id: str, cooldown_until: float):
        import time
        remaining = max(0, cooldown_until - time.time())
        super().__init__(
            message=f"Deployment '{deployment_id}' is in cooldown for {remaining:.1f}s",
            details={
                "deployment_id": deployment_id,
                "cooldown_until": cooldown_until,
                "remaining_seconds": remaining
            }
        )

class DeploymentFailed(DeploymentError):
    """部署已失败"""

    def __init__(self, deployment_id: str, consecutive_failures: int):
        super().__init__(
            message=f"Deployment '{deployment_id}' has failed {consecutive_failures} times",
            details={
                "deployment_id": deployment_id,
                "consecutive_failures": consecutive_failures
            }
        )

class DeploymentTimeoutError(DeploymentError):
    """部署请求超时"""

    def __init__(self, deployment_id: str, timeout: float):
        super().__init__(
            message=f"Deployment '{deployment_id}' timed out after {timeout}s",
            details={"deployment_id": deployment_id, "timeout": timeout}
        )

class DeploymentAuthError(DeploymentError):
    """认证失败 (401, 403)"""

    def __init__(self, deployment_id: str, status_code: int):
        super().__init__(
            message=f"Deployment '{deployment_id}' returned HTTP {status_code} (auth failed)",
            details={"deployment_id": deployment_id, "status_code": status_code}
        )

class DeploymentRateLimitError(DeploymentError):
    """限流 (429)"""

    def __init__(self, deployment_id: str, retry_after: Optional[float] = None):
        msg = f"Deployment '{deployment_id}' rate limited"
        if retry_after is not None:
            msg += f", retry after {retry_after}s"
        super().__init__(
            message=msg,
            details={"deployment_id": deployment_id, "retry_after": retry_after}
        )

class DeploymentServerError(DeploymentError):
    """服务器错误 (5xx)"""

    def __init__(self, deployment_id: str, status_code: int, response_body: str = ""):
        super().__init__(
            message=f"Deployment '{deployment_id}' returned HTTP {status_code} (server error)",
            details={"deployment_id": deployment_id, "status_code": status_code, "response_body": response_body}
        )

class DeploymentNetworkError(DeploymentError):
    """网络连接错误"""

    def __init__(self, deployment_id: str, reason: str):
        super().__init__(
            message=f"Deployment '{deployment_id}' network error: {reason}",
            details={"deployment_id": deployment_id, "reason": reason}
        )

# ============ 缓存异常 ============

class CacheError(SdkLlmError):
    """缓存基础异常"""
    pass

class CacheKeyNotFound(CacheError):
    """缓存键不存在"""

    def __init__(self, key: str):
        super().__init__(
            message=f"Cache key '{key}' not found",
            details={"key": key}
        )

class CacheExpired(CacheError):
    """缓存已过期"""

    def __init__(self, key: str, expired_at: float):
        super().__init__(
            message=f"Cache key '{key}' has expired",
            details={"key": key, "expired_at": expired_at}
        )

# ============ 健康检查异常 ============

class HealthCheckError(SdkLlmError):
    """健庭检查基础异常"""
    pass

class HealthCheckFailed(HealthCheckError):
    """健庭检查失败"""

    def __init__(self, deployment_id: str, error: str):
        super().__init__(
            message=f"Health check failed for deployment '{deployment_id}'",
            details={"deployment_id": deployment_id, "error": error}
        )

class HealthCheckTimeout(HealthCheckError):
    """健庭检查超时"""

    def __init__(self, deployment_id: str, timeout: float):
        super().__init__(
            message=f"Health check timed out for deployment '{deployment_id}' after {timeout}s",
            details={"deployment_id": deployment_id, "timeout": timeout}
        )

# ============ 配置异常 ============

class ConfigError(SdkLlmError):
    """配罫基础异常"""
    pass

class InvalidDeploymentConfig(ConfigError):
    """无效部署配罫"""

    def __init__(self, field: str, value: Any, reason: str):
        super().__init__(
            message=f"Invalid deployment config: field '{field}' with value '{value}' - {reason}",
            details={"field": field, "value": str(value), "reason": reason}
        )

class MissingRequiredField(ConfigError):
    """缺少必需字段"""

    def __init__(self, field: str, context: str):
        super().__init__(
            message=f"Missing required field '{field}' in {context}",
            details={"field": field, "context": context}
        )

# ============ 异常注册表 ============

# 异常类型到HTTP状态码映射
ERROR_STATUS_MAP = {
    NoDeploymentAvailable: 503,
    AllDeploymentsFailed: 503,
    DeploymentInCooldown: 503,
    DeploymentFailed: 503,
    DeploymentTimeoutError: 504,
    DeploymentAuthError: 401,
    DeploymentRateLimitError: 429,
    DeploymentServerError: 502,
    DeploymentNetworkError: 502,
    CacheKeyNotFound: 404,
    CacheExpired: 410,
    HealthCheckFailed: 503,
    HealthCheckTimeout: 504,
    InvalidDeploymentConfig: 400,
    MissingRequiredField: 400,
    RouterError: 500,
    DeploymentError: 500,
    CacheError: 500,
    HealthCheckError: 500,
    ConfigError: 400,
    SdkLlmError: 500,
}

def get_status_code(error: Exception) -> int:
    """获取异常对应的HTTP状态码"""
    for error_class in type(error).__mro__:
        if error_class in ERROR_STATUS_MAP:
            return ERROR_STATUS_MAP[error_class]
    return 500
