"""Public A4P protocol types.

The wire format uses camelCase because these objects cross Python, HTTP, and frontend boundaries.
"""

from __future__ import annotations

from dataclasses import asdict, dataclass, field, is_dataclass
from typing import Any, Literal, TypedDict


JsonDict = dict[str, Any]


class IntentMandate(TypedDict):
    type: Literal["a4p/v1/intent-mandate"]
    mandateId: str
    server: str
    subject: JsonDict
    intent: JsonDict
    validTime: JsonDict
    userAuthorization: JsonDict
    displayText: str
    signatures: JsonDict


class IntentToken(TypedDict):
    type: Literal["a4p/v1/intent-token"]
    tokenId: str
    mandateId: str
    subject: JsonDict
    user: JsonDict
    intent: JsonDict
    issuedAt: str
    expireAt: str
    nonce: str
    signature: str
    alg: str
    keyId: str


class OperationMandate(TypedDict):
    type: Literal["a4p/v1/operation-mandate"]
    operationId: str
    server: str
    subject: JsonDict
    operation: JsonDict
    validTime: JsonDict
    userAuthorization: JsonDict
    displayText: str
    signatures: JsonDict


def to_payload(value: Any) -> Any:
    """Convert A4P values to JSON-serializable payloads."""
    if is_dataclass(value):
        return {key: to_payload(item) for key, item in asdict(value).items() if item is not None}
    if isinstance(value, dict):
        return {str(key): to_payload(item) for key, item in value.items() if item is not None}
    if isinstance(value, list):
        return [to_payload(item) for item in value]
    return value


@dataclass(frozen=True)
class VerificationResult:
    valid: bool
    reason: str | None = None
    code: str | None = None
    matchedScope: JsonDict | None = None

    @classmethod
    def ok(cls, matched_scope: JsonDict | None = None) -> "VerificationResult":
        return cls(valid=True, matchedScope=matched_scope)

    @classmethod
    def fail(cls, reason: str, code: str = "AUTHORIZATION_INVALID") -> "VerificationResult":
        return cls(valid=False, reason=reason, code=code)


@dataclass(frozen=True)
class UserAuthorizationRequest:
    mandate: IntentMandate | OperationMandate
    signingOptions: JsonDict = field(default_factory=dict)


@dataclass(frozen=True)
class UserAuthorizationResponse:
    approved: bool
    signedMandate: IntentMandate | OperationMandate | None = None
    rejectReason: str | None = None
    errorCode: str | None = None


@dataclass(frozen=True)
class IntentAuthorizationRequest:
    agentId: str
    userId: str
    intent: JsonDict
    validitySeconds: int | None = None
    agentPublicKey: JsonDict | None = None
    metadata: JsonDict = field(default_factory=dict)


@dataclass(frozen=True)
class IntentAuthorizationResponse:
    mandate: IntentMandate | None = None
    signingOptions: JsonDict = field(default_factory=dict)
    intentToken: IntentToken | None = None
    verificationResult: VerificationResult | None = None
    approved: bool = False
    rejectReason: str | None = None


@dataclass(frozen=True)
class TokenVerificationRequest:
    token: IntentToken
    expected: JsonDict


@dataclass(frozen=True)
class TokenVerificationResponse:
    valid: bool
    reason: str | None = None
    code: str | None = None
    matchedScope: JsonDict | None = None


@dataclass(frozen=True)
class OperationAuthorizationRequest:
    agentId: str
    userId: str
    operation: JsonDict
    validitySeconds: int | None = None
    agentPublicKey: JsonDict | None = None
    metadata: JsonDict = field(default_factory=dict)


@dataclass(frozen=True)
class OperationAuthorizationCompletionRequest:
    signedMandate: OperationMandate
    operation: JsonDict


@dataclass(frozen=True)
class OperationAuthorizationChallenge:
    mandate: OperationMandate | None = None
    signingOptions: JsonDict = field(default_factory=dict)
    verificationResult: VerificationResult | None = None
    rejectReason: str | None = None


@dataclass(frozen=True)
class OperationAuthorizationResult:
    operationId: str | None = None
    approved: bool = False
    verificationResult: VerificationResult | None = None
    rejectReason: str | None = None


__all__ = [
    "IntentAuthorizationRequest",
    "IntentAuthorizationResponse",
    "IntentMandate",
    "IntentToken",
    "JsonDict",
    "OperationAuthorizationChallenge",
    "OperationAuthorizationCompletionRequest",
    "OperationAuthorizationRequest",
    "OperationAuthorizationResult",
    "OperationMandate",
    "TokenVerificationRequest",
    "TokenVerificationResponse",
    "UserAuthorizationRequest",
    "UserAuthorizationResponse",
    "VerificationResult",
    "to_payload",
]
