"""Image management pydantic request / response models.

``runtime_spec`` is an opaque JSON object passthrough (no ``ImageSpec``
typed structure). ``env_vars`` / ``workspace`` / ``mounts`` are top-level
fields alongside ``runtime_spec``, aligning with the yuanrong
``CreateAgentRequest`` layout.
"""

from __future__ import annotations

from typing import Any, Dict, List, Optional

from pydantic import BaseModel, Field


# DEPRECATED: ImageSpec is no longer used by any endpoint.
# runtime_spec is passed through as an opaque JSON object.


class RegisterImageRequest(BaseModel):
    """``POST /api/images`` request body."""

    framework: str = Field(..., description="Framework name, e.g. opencode")
    framework_version: str = Field(..., description="Framework version, e.g. v0.2.0")
    runtime_spec: Dict[str, Any] = Field(
        ..., description="Opaque yuanrong RuntimeSpec JSON (passthrough)"
    )
    env_vars: Dict[str, str] = Field(
        default_factory=dict, description="Environment variables"
    )
    workspace: Optional[str] = Field(None, description="Working directory")
    mounts: List[Dict[str, Any]] = Field(
        default_factory=list, description="Volume mounts"
    )
    image_module_version: Optional[str] = Field(
        None, description="Image-processing module version"
    )
    uploaded_by: str = Field(..., description="Uploader identity")


class SetDefaultRequest(BaseModel):
    """``PUT /api/images/{framework}/default`` request body."""

    framework_version: str = Field(..., description="Version to set as default")


class ImageRegisterResponse(BaseModel):
    framework: str
    framework_version: str
    is_default: bool
    status: str  # "registered" | "updated"


class ImageEntry(BaseModel):
    """One flat row from the image registry (one framework version).

    ``runtime_spec`` is an opaque JSON passthrough.
    """

    framework: str
    framework_version: str
    is_default: bool
    image_module_version: Optional[str] = None
    runtime_spec: Optional[Dict[str, Any]] = None
    workspace: Optional[str] = None
    mounts: List[Dict[str, Any]] = Field(default_factory=list)
    env_vars: Dict[str, str] = Field(default_factory=dict)
    uploaded_by: Optional[str] = None
    created_at: Optional[str] = None


class LaunchSpecResponse(BaseModel):
    """``GET /api/images/{framework}/launch-spec`` output.

    ``runtime_spec`` is opaque JSON passthrough; ``env_vars`` / ``workspace``
    / ``mounts`` are top-level fields.
    """

    framework: str
    framework_version: str
    runtime_spec: Optional[Dict[str, Any]] = None
    env_vars: Dict[str, str] = Field(default_factory=dict)
    workspace: Optional[str] = None
    mounts: List[Dict[str, Any]] = Field(default_factory=list)
    image_module_version: Optional[str] = None


class DeregisterResponse(BaseModel):
    framework: str
    framework_version: str
    status: str  # "deregistered"
    repo_deleted: bool = False


class SetDefaultResponse(BaseModel):
    framework: str
    default: str
    status: str  # "updated"
