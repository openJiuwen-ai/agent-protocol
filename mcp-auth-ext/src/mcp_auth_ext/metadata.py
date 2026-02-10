"""Extended protected resource metadata with optional authorization_schemes."""

from pydantic import BaseModel, Field

from mcp.shared.auth import ProtectedResourceMetadata


class AuthSchemeDescriptor(BaseModel):
    """Descriptor for one supported authorization scheme in extended PRM.

    scheme_id identifies the scheme (e.g. oauth2, oauth2_client_credentials,
    API key). params provides scheme-specific information
    to the client (e.g. header_name, param_name) as key-value pairs.
    """

    scheme_id: str = Field(..., description="e.g. oauth2, api_key")
    params: dict[str, str] | None = Field(
        default=None,
        description="Optional scheme-specific parameters for the client (e.g. header_name)",
    )


class ExtendedProtectedResourceMetadata(ProtectedResourceMetadata):
    """RFC 9728 Protected Resource Metadata plus optional authorization_schemes.

    Servers can add authorization_schemes to the same JSON document; SDK clients
    ignore it (extra fields), extension clients use it to choose an auth scheme.
    """

    authorization_schemes: list[AuthSchemeDescriptor] | None = Field(
        default=None,
        description="Optional list of supported authorization schemes.",
    )
