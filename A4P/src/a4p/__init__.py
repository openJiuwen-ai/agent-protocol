"""Public A4P SDK exports."""

from a4p.client import A4PClient
from a4p.credential_store import (
    CredentialStoreFormatError,
    InMemoryCredentialStore,
    JsonFileCredentialStore,
    UserCredentialRecord,
)
from a4p.errors import (
    A4PProtocolError,
    CredentialKeyConflictError,
    SignatureMethodNotEnabledError,
    UserCredentialNotRegisteredError,
)
from a4p.intent.mandate import IntentDisplayTextRenderer
from a4p.intent.usage_store import A4PIntentTokenUsageStore, SQLiteIntentTokenUsageStore
from a4p.mandate_security import (
    MandateSecurityError,
    StaticA4PServerTrustStore,
    derive_user_authorization_challenge,
    mandate_identifier,
    user_authorization_challenge_base64url,
    verify_trusted_server_mandate,
)
from a4p.operation.mandate import OperationDisplayTextRenderer
from a4p.server import A4PServer
from a4p.types import (
    IntentAuthorizationRequest,
    IntentAuthorizationResponse,
    IntentMandate,
    IntentToken,
    OperationAuthorizationChallenge,
    OperationAuthorizationCompletionRequest,
    OperationAuthorizationRequest,
    OperationAuthorizationResult,
    OperationMandate,
    TokenVerificationRequest,
    TokenVerificationResponse,
    UserAuthorizationRequest,
    UserAuthorizationResponse,
    VerificationResult,
)
from a4p.user_authorizer import (
    A4PUserAuthorizer,
    ApprovingA4PUserAuthorizer,
    RejectingA4PUserAuthorizer,
    approve_user_mandate,
    sign_user_mandate_with_signer,
    verify_local_user_authorization_request,
)
from a4p.user_signature import (
    A4PUserSignatureMethod,
    A4PUserSigner,
    UserSignatureContext,
)

__all__ = [
    "A4PClient",
    "A4PProtocolError",
    "A4PServer",
    "A4PIntentTokenUsageStore",
    "A4PUserAuthorizer",
    "A4PUserSignatureMethod",
    "A4PUserSigner",
    "ApprovingA4PUserAuthorizer",
    "CredentialKeyConflictError",
    "CredentialStoreFormatError",
    "InMemoryCredentialStore",
    "IntentDisplayTextRenderer",
    "IntentAuthorizationRequest",
    "IntentAuthorizationResponse",
    "IntentMandate",
    "IntentToken",
    "JsonFileCredentialStore",
    "MandateSecurityError",
    "OperationAuthorizationChallenge",
    "OperationAuthorizationCompletionRequest",
    "OperationAuthorizationRequest",
    "OperationAuthorizationResult",
    "OperationDisplayTextRenderer",
    "OperationMandate",
    "RejectingA4PUserAuthorizer",
    "SignatureMethodNotEnabledError",
    "approve_user_mandate",
    "sign_user_mandate_with_signer",
    "SQLiteIntentTokenUsageStore",
    "StaticA4PServerTrustStore",
    "TokenVerificationRequest",
    "TokenVerificationResponse",
    "UserAuthorizationRequest",
    "UserAuthorizationResponse",
    "UserCredentialNotRegisteredError",
    "UserCredentialRecord",
    "UserSignatureContext",
    "VerificationResult",
    "derive_user_authorization_challenge",
    "mandate_identifier",
    "user_authorization_challenge_base64url",
    "verify_local_user_authorization_request",
    "verify_trusted_server_mandate",
]
