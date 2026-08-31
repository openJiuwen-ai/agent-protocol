"""Instance management module — register / query / update / deregister
instances (三方 / 九问 unified flow).

The registry only stores instance references plus their node/address; it
does not invoke the runtime (元戎) or make decisions for the gateway.
The gateway calls ``POST /api/instances`` after launching an instance;
``GET /api/instances`` returns the list with the persisted ``status``.

Persistence goes through ``RegistryTableService`` (SQL backend); this
module does not hold a store/backend directly. ``status`` (运行 / 停止 /
异常) is persisted inside the row's ``data`` JSON and written by the
gateway via ``PATCH /api/instances/{service_id}`` (元戎 List) — the
registry does NOT receive heartbeats or derive status.
"""

from .errors import InstanceNotFoundError, InstanceValidationError
from .service import InstanceService
from .deps import get_instance_service, set_instance_service
from .router import router

__all__ = [
    "InstanceService",
    "InstanceNotFoundError",
    "InstanceValidationError",
    "get_instance_service",
    "set_instance_service",
    "router",
]
