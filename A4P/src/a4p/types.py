"""Public A4P protocol types.

The wire format intentionally uses camelCase because these objects cross Python,
HTTP, and frontend boundaries.
"""

from __future__ import annotations

from dataclasses import asdict, dataclass, field, is_dataclass
from typing import Any, Literal


JsonDict = dict[str, Any]


def to_payload(value: Any) -> Any:
    """Convert A4P dataclasses to JSON-serializable payloads."""
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
    requestId: str
    mandate: JsonDict
    uiContext: JsonDict = field(default_factory=dict)


@dataclass(frozen=True)
class UserAuthorizationResponse:
    requestId: str
    approved: bool
    signedMandate: JsonDict | None = None
    rejectReason: str | None = None
    approvedAt: str | None = None


@dataclass(frozen=True)
class IntentAuthorizationRequest:
    agentId: str
    userId: str
    intent: JsonDict
    validitySeconds: int | None = None
    metadata: JsonDict = field(default_factory=dict)


@dataclass(frozen=True)
class IntentAuthorizationResponse:
    requestId: str
    mandate: JsonDict | None = None
    intentToken: JsonDict | None = None
    verificationResult: VerificationResult | None = None
    approved: bool = False
    rejectReason: str | None = None


@dataclass(frozen=True)
class TokenVerificationRequest:
    token: JsonDict
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
    metadata: JsonDict = field(default_factory=dict)


@dataclass(frozen=True)
class OperationAuthorizationResponse:
    requestId: str
    mandate: JsonDict | None = None
    signedMandate: JsonDict | None = None
    verificationResult: VerificationResult | None = None
    approved: bool = False
    rejectReason: str | None = None


@dataclass(frozen=True)
class IntentMandate:
    type: Literal["a4p/v1/intent-mandate"] = "a4p/v1/intent-mandate"
    mandateId: str = ""
    server: str = ""
    subject: JsonDict = field(default_factory=dict)
    intent: JsonDict = field(default_factory=dict)
    validTime: JsonDict = field(default_factory=dict)
    displayText: str = ""
    signatures: JsonDict = field(default_factory=dict)


@dataclass(frozen=True)
class IntentToken:
    type: Literal["a4p/v1/intent-token"] = "a4p/v1/intent-token"
    tokenId: str = ""
    mandateId: str = ""
    subject: JsonDict = field(default_factory=dict)
    user: JsonDict = field(default_factory=dict)
    intent: JsonDict = field(default_factory=dict)
    issuedAt: str = ""
    expireAt: str = ""
    nonce: str = ""
    signature: str = ""
    alg: str = ""
    keyId: str = ""


@dataclass(frozen=True)
class OperationMandate:
    type: Literal["a4p/v1/operation-mandate"] = "a4p/v1/operation-mandate"
    operationId: str = ""
    server: str = ""
    subject: JsonDict = field(default_factory=dict)
    operation: JsonDict = field(default_factory=dict)
    validTime: JsonDict = field(default_factory=dict)
    displayText: str = ""
    signatures: JsonDict = field(default_factory=dict)


__all__ = [
    "IntentAuthorizationRequest",
    "IntentAuthorizationResponse",
    "IntentMandate",
    "IntentToken",
    "JsonDict",
    "OperationAuthorizationRequest",
    "OperationAuthorizationResponse",
    "OperationMandate",
    "TokenVerificationRequest",
    "TokenVerificationResponse",
    "UserAuthorizationRequest",
    "UserAuthorizationResponse",
    "VerificationResult",
    "to_payload",
]
