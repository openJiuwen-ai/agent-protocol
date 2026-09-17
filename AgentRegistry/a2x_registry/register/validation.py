"""Unified format validation for registered services.

Each service type (generic / a2a / skill) has its own `FormatValidator`
subclass with an ordered list of supported protocol versions
(``SUPPORTED_VERSIONS``, oldest → newest). A dataset's
``register_config.json`` declares, per type, the oldest version that is still
accepted (``min_version``). Validation runs from that version upwards and
succeeds as soon as any version's required-field check passes.

Design goals:
  - High cohesion: every type owns its own field-level checks.
  - Low coupling: the service layer only talks to ``validate_service`` /
    ``validate_agent_card`` — never to a specific validator subclass.
  - Extension: new versions slot into ``SUPPORTED_VERSIONS`` + a new
    ``_check_version`` branch, no other file changes.

All v0.0 validators check only ``name`` and ``description`` — any payload with
a non-empty name+description passes v0.0 and therefore the whole check.
"""

from abc import ABC, abstractmethod
from dataclasses import dataclass, field
from typing import Any, Dict, List, Optional, Set, Tuple

from .models import AgentCard

# ---------------------------------------------------------------------------
# Result
# ---------------------------------------------------------------------------

@dataclass
class ValidationResult:
    """Outcome of a format-validation run.

    ``matched_version`` is the first (oldest) version that passed, or ``None``
    when nothing passed. ``service_type`` identifies which validator produced
    the result (useful when errors bubble up from a dispatcher).
    """
    valid: bool
    service_type: Optional[str] = None
    matched_version: Optional[str] = None
    errors: List[str] = field(default_factory=list)
    warnings: List[str] = field(default_factory=list)


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _has_text(value: Any) -> bool:
    """True iff the value is a non-empty, non-whitespace string."""
    return bool(value and isinstance(value, str) and value.strip())


def _version_key(version: str) -> Tuple[int, ...]:
    """Turn 'v0.0' / 'v1.2.3' into a sortable tuple of ints."""
    if not isinstance(version, str) or not version:
        raise ValueError(f"Invalid version: {version!r}")
    v = version[1:] if version.startswith("v") else version
    try:
        return tuple(int(p) for p in v.split("."))
    except ValueError as exc:
        raise ValueError(f"Invalid version string: {version!r}") from exc


def _to_dict(obj: Any) -> dict:
    """Normalize a Pydantic model / dict / None into a plain dict."""
    if obj is None:
        return {}
    if isinstance(obj, dict):
        return obj
    if hasattr(obj, "model_dump"):
        return obj.model_dump()
    if hasattr(obj, "__dict__"):
        return dict(obj.__dict__)
    return {}


# ---------------------------------------------------------------------------
# Base class
# ---------------------------------------------------------------------------

class FormatValidator(ABC):
    """Abstract base for one service type's validator.

    Subclass contract:
      - Set ``service_type`` to the unique type key ("generic" / "a2a" / "skill").
      - Populate ``SUPPORTED_VERSIONS`` ordered oldest → newest.
      - Implement ``_check_version(payload, version)`` returning
        ``(errors, warnings)`` — empty errors means the version passed.

    ``validate(payload, min_version)`` handles version iteration and result
    assembly. Subclasses don't touch it.
    """

    service_type: str = ""
    SUPPORTED_VERSIONS: List[str] = []

    def validate(self, payload: Any, min_version: str = "v0.0") -> ValidationResult:
        """Try each supported version ≥ ``min_version``, oldest first.

        Returns on the first passing version. If none pass, returns a failure
        result carrying the *newest* version's errors (most informative).
        """
        try:
            threshold = _version_key(min_version)
        except ValueError as exc:
            return ValidationResult(
                valid=False, service_type=self.service_type, errors=[str(exc)])

        versions_to_try = [v for v in self.SUPPORTED_VERSIONS
                           if _version_key(v) >= threshold]
        if not versions_to_try:
            return ValidationResult(
                valid=False, service_type=self.service_type,
                errors=[f"No {self.service_type} version ≥ {min_version}. "
                        f"Supported: {self.SUPPORTED_VERSIONS}"])

        data = _to_dict(payload)
        last_errors: List[str] = []
        last_warnings: List[str] = []
        for version in versions_to_try:
            errs, warns = self._check_version(data, version)
            if not errs:
                return ValidationResult(
                    valid=True, service_type=self.service_type,
                    matched_version=version, warnings=warns)
            last_errors, last_warnings = errs, warns

        return ValidationResult(
            valid=False, service_type=self.service_type,
            errors=[f"No allowed {self.service_type} version matched "
                    f"{versions_to_try}. Latest errors: " + "; ".join(last_errors)],
            warnings=last_warnings)

    @abstractmethod
    def _check_version(self, payload: dict, version: str) -> Tuple[List[str], List[str]]:
        """Return ``(errors, warnings)`` for the given version."""
        ...

    @staticmethod
    def _check_name_description(payload: dict) -> Tuple[List[str], List[str]]:
        """Shared v0.0 baseline: requires name + description only."""
        errs: List[str] = []
        if not _has_text(payload.get("name")):
            errs.append("name is required")
        if not _has_text(payload.get("description")):
            errs.append("description is required")
        return errs, []


# ---------------------------------------------------------------------------
# Concrete validators
# ---------------------------------------------------------------------------

class GenericValidator(FormatValidator):
    service_type = "generic"
    SUPPORTED_VERSIONS = ["v0.0"]

    def _check_version(self, payload, version):
        if version == "v0.0":
            return self._check_name_description(payload)
        return [f"Unknown {self.service_type} version {version}"], []


class SkillValidator(FormatValidator):
    service_type = "skill"
    SUPPORTED_VERSIONS = ["v0.0"]

    def _check_version(self, payload, version):
        if version == "v0.0":
            return self._check_name_description(payload)
        return [f"Unknown {self.service_type} version {version}"], []


class A2AValidator(FormatValidator):
    """A2A AgentCard validator — versions v0.0 (loose) and v1.0 (full spec)."""

    service_type = "a2a"
    SUPPORTED_VERSIONS = ["v0.0", "v1.0"]

    def _check_version(self, payload, version):
        if version == "v0.0":
            return self._v0_0(payload)
        if version == "v1.0":
            return self._v1_0(payload)
        return [f"Unknown {self.service_type} version {version}"], []

    @classmethod
    def _v0_0(cls, payload: dict) -> Tuple[List[str], List[str]]:
        errs, warns = cls._check_name_description(payload)
        if not payload.get("version"):
            warns.append("version is recommended")
        if not payload.get("skills"):
            warns.append("skills is recommended (agent has no declared capabilities)")
        return errs, warns

    @staticmethod
    def _v1_0(payload: dict) -> Tuple[List[str], List[str]]:
        errs: List[str] = []
        warns: List[str] = []

        # Top-level required strings
        if not _has_text(payload.get("name")):
            errs.append("name is required")
        if not _has_text(payload.get("description")):
            errs.append("description is required")
        if not _has_text(payload.get("version")):
            errs.append("version is required")
        if not _has_text(payload.get("url")):
            errs.append("url (or supported_interfaces) is required")

        if payload.get("capabilities") is None:
            errs.append("capabilities is required (can be empty object)")
        if not payload.get("defaultInputModes"):
            errs.append('defaultInputModes is required (e.g. ["text/plain"])')
        if not payload.get("defaultOutputModes"):
            errs.append('defaultOutputModes is required (e.g. ["text/plain"])')

        skills = payload.get("skills")
        if not skills:
            errs.append("skills is required (at least one skill)")
        else:
            for i, sk in enumerate(skills):
                s = _to_dict(sk)
                p = f"skills[{i}]"
                if not _has_text(s.get("id")):
                    errs.append(f"{p}.id is required")
                if not _has_text(s.get("name")):
                    errs.append(f"{p}.name is required")
                if not _has_text(s.get("description")):
                    errs.append(f"{p}.description is required")
                if not s.get("tags"):
                    errs.append(f"{p}.tags is required (at least one tag)")

        prov = payload.get("provider")
        prov_dict = _to_dict(prov) if prov else {}
        if prov_dict:
            if not _has_text(prov_dict.get("organization")):
                errs.append("provider.organization is required when provider is present")
            if not _has_text(prov_dict.get("url")):
                errs.append("provider.url is required when provider is present")

        if not _has_text(payload.get("protocolVersion")):
            warns.append('protocolVersion is recommended (e.g. "1.0")')
        if not prov_dict:
            warns.append("provider is recommended")
        if not _has_text(payload.get("documentationUrl")):
            warns.append("documentationUrl is recommended")

        return errs, warns


# ---------------------------------------------------------------------------
# Public API
# ---------------------------------------------------------------------------

VALIDATORS: Dict[str, FormatValidator] = {
    GenericValidator.service_type: GenericValidator(),
    A2AValidator.service_type:     A2AValidator(),
    SkillValidator.service_type:   SkillValidator(),
}

SUPPORTED_SERVICE_TYPES: Tuple[str, ...] = tuple(VALIDATORS.keys())

# Default per-dataset config: all three types allowed, each from v0.0 upwards.
DEFAULT_FORMAT_CONFIG: Dict[str, str] = {t: "v0.0" for t in SUPPORTED_SERVICE_TYPES}


def validate_service(service_type: str, payload: Any,
                     min_version: str = "v0.0") -> ValidationResult:
    """Validate a payload for the given service type at or above ``min_version``.

    Dispatches to the appropriate ``FormatValidator``. Returns a result with
    ``matched_version`` set to the first (oldest) version that passed.
    """
    validator = VALIDATORS.get(service_type)
    if validator is None:
        return ValidationResult(
            valid=False, service_type=service_type,
            errors=[f"Unknown service type {service_type!r}. "
                    f"Supported: {list(VALIDATORS)}"])
    return validator.validate(payload, min_version)


def normalize_format_config(raw: Optional[Dict[str, Any]]) -> Dict[str, str]:
    """Sanitize a user-supplied formats dict.

    - Drops unknown types.
    - Drops types whose value references an unknown version.
    - Accepts either ``{type: "v0.0"}`` or ``{type: {"min_version": "v0.0"}}``.
    - Returns empty dict if the result is empty; callers substitute defaults.
    """
    if not isinstance(raw, dict):
        return {}
    out: Dict[str, str] = {}
    for t, v in raw.items():
        if t not in VALIDATORS:
            continue
        if isinstance(v, dict):
            v = v.get("min_version", "v0.0")
        if not isinstance(v, str):
            continue
        if v not in VALIDATORS[t].SUPPORTED_VERSIONS:
            continue
        out[t] = v
    return out


# ---------------------------------------------------------------------------
# Legacy shim — kept so existing callers (pre-refactor) keep working.
# ---------------------------------------------------------------------------

DEFAULT_ALLOWED_VERSIONS: Set[str] = set(A2AValidator.SUPPORTED_VERSIONS)


def validate_agent_card(card: AgentCard,
                        allowed_versions: Optional[Set[str]] = None) -> ValidationResult:
    """Validate an AgentCard against a set of allowed A2A versions.

    Translation: the oldest version in ``allowed_versions`` becomes the
    ``min_version`` passed to the unified ``validate_service`` pipeline.
    New code should call ``validate_service("a2a", …)`` directly.
    """
    if allowed_versions is None:
        allowed_versions = DEFAULT_ALLOWED_VERSIONS
    known = [v for v in allowed_versions if v in A2AValidator.SUPPORTED_VERSIONS]
    if not known:
        return ValidationResult(
            valid=False, service_type="a2a",
            errors=[f"No known A2A versions in allowed set: {allowed_versions}"])
    min_version = min(known, key=_version_key)
    return validate_service("a2a", card, min_version)
