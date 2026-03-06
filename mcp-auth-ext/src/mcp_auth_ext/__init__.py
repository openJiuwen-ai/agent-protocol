"""Extended authorization for MCP: multiple schemes via protected resource metadata."""

from mcp_auth_ext.discovery import fetch_extended_protected_resource_metadata
from mcp_auth_ext.metadata import AuthSchemeDescriptor, ExtendedProtectedResourceMetadata

__all__ = [
    "AuthSchemeDescriptor",
    "ExtendedProtectedResourceMetadata",
    "fetch_extended_protected_resource_metadata",
]
