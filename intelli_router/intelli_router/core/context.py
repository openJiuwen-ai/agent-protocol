"""
SDK LLM Core - 路由上下文

RoutingContext: 请求级上下文
"""
from dataclasses import dataclass, field
from typing import Optional, List, Dict, Any, TYPE_CHECKING
import time

from .deployment import Deployment

if TYPE_CHECKING:
    from .deployment import DeploymentStatus

@dataclass
class RoutingContext:
    """
    路由上下文 - 请求级别

    Attributes:
    model: str 请求模型
    messages: List[Dict[str, str]] 消息列表
    kwargs: Dict[str, Any] 其他参数
    selected_deployment: Optional[Deployment] 选中的部署
    attempted_deployments: List[str] 尝试过的部署
    start_time: float 开时时间
    end_time: Optional[float] 束时间
    response: Optional[Any] 果果
    error: Optional[Exception] 误
    request_tags: List[str] 求签
    """
    # 请求信息
    model: str
    messages: List[Dict[str, str]]
    kwargs: Dict[str, Any] = field(default_factory=dict)

    # 路由状态
    selected_deployment: Optional[Deployment] = None
    attempted_deployments: List[str] = field(default_factory=list)

    # 时间追踪
    start_time: float = field(default_factory=time.time)
    end_time: Optional[float] = None
    # 结果
    response: Optional[Any] = None
    error: Optional[Exception] = None
    # 标签
    request_tags: List[str] = field(default_factory=list)


    def mark_attempt(self, deployment: Deployment) -> None:
        """标记尝试过的部署"""
        self.attempted_deployments.append(deployment.id)

    def set_success(self, deployment: Deployment, response: Any) -> None:
        """标记成功"""
        self.selected_deployment = deployment
        self.response = response
        self.end_time = time.time()

    def set_failure(self, error: Exception) -> None:
        """标记失败"""
        self.error = error
        self.end_time = time.time()

    @property
    def latency(self) -> float:
        """获取请求延迟"""
        if self.end_time is None:
            return 0.0
        return self.end_time - self.start_time

    def to_log_dict(self) -> Dict[str, Any]:
        """转换为日志字典"""
        return {
            "model": self.model,
            "selected_deployment": self.selected_deployment.id if self.selected_deployment else None,
            "latency": self.latency,
            "attempted": self.attempted_deployments,
            "success": self.error is None,
        }
