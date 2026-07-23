"""Instance management module — register / query / update / deregister
instances (三方 / 九问 unified flow).

The registry only stores instance references plus their node/address; it
does not invoke the runtime (元戎) or make decisions for the gateway.
The gateway calls ``POST /api/instances`` after launching an instance;
``GET /api/instances`` returns the list with derived ``status``.

Persistence goes through ``RegistryTableService`` (SQL backend); this
module does not hold a store/backend directly. ``status`` (运行 / 异常)
is derived per-query from a node-heartbeat callback injected via
``set_heartbeat_check`` (heartbeat module wires the real callback).
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
