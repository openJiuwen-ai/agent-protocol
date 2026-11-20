"""Image management module — register / query / deregister third-party
framework images, with multi-version and default-version management.

The registry only stores image references plus launch specs (in the image
registry table); it does not invoke the image-processing module or make
decisions for the gateway. The image-processing module calls
``POST /api/images`` to register; the gateway calls
``GET /api/images/{fw}/launch-spec`` to fetch the launch spec before
spawning a process.

Persistence goes through ``RegistryTableService`` (SQL backend); this
module does not hold a store/backend directly.
"""

from .errors import ImageNotFoundError, ImageValidationError, RepoDeleteError
from .service import ImageService
from .deps import get_image_service, set_image_service
from .router import router

__all__ = [
    "ImageService",
    "ImageNotFoundError",
    "ImageValidationError",
    "RepoDeleteError",
    "get_image_service",
    "set_image_service",
    "router",
]
