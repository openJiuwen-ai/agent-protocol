"""
SDK LLM Core - 部署配罫

Deployment: 一个API端点
DeploymentStatus: 状态枚举
"""
from dataclasses import dataclass, field
from enum import Enum
from typing import Optional, List, Dict, Any
from datetime import datetime
import uuid

class DeploymentStatus(Enum):
    """部署状态枚举"""
    HEALTHY = "healthy"
    COOLDOWN = "cooldown"
    FAILED = "failed"

@dataclass
class Deployment:
    """
    部署配罫 - 一个API端点

    Attributes:
        model_name: str             型号名
        api_key: str API密钥
        api_base: str API端点URL
    id: str 部署ID (自动生成)
    status: DeploymentStatus 状态
    consecutive_failures: int 连续失败次数
    cooldown_until: Optional[float] 冷却时间戳
    tags: List[str] 标签列表
    """
    model_name: str
    api_key: str
    api_base: str

    # 以下字段有默认值
    id: str = field(default_factory=lambda: str(uuid.uuid4())[:8])
    status: DeploymentStatus = DeploymentStatus.HEALTHY
    consecutive_failures: int = 0
    cooldown_until: Optional[float] = None
    tags: List[str] = field(default_factory=list)


    # 模型信息
    tpm: Optional[int] = None
    rpm: Optional[int] = None
    timeout: Optional[float] = None

    verify_ssl: bool = True
    litellm_params: Dict[str, Any] = field(default_factory=dict)


    def to_dict(self) -> Dict[str, Any]:
        """序列化为字典"""
        return {
            "id": self.id,
            "model_name": self.model_name,
            "api_key": self.api_key,
            "api_base": self.api_base,
            "status": self.status.value,
            "consecutive_failures": self.consecutive_failures,
            "cooldown_until": self.cooldown_until,
            "tags": self.tags,
            "tpm": self.tpm,
            "rpm": self.rpm,
            "timeout": self.timeout,
            "verify_ssl": self.verify_ssl,
            "litellm_params": self.litellm_params,
        }

    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> "Deployment":
        """从字典反序列化"""
        data["status"] = DeploymentStatus(data.get("status", "healthy"))
        return cls(**data)

    def is_available(self, now: float) -> bool:
        """检查部署是否可用"""
        if self.status == DeploymentStatus.FAILED:
            return False
        if self.status == DeploymentStatus.COOLDOWN:
            if self.cooldown_until and now < self.cooldown_until:
                return False
        return True
