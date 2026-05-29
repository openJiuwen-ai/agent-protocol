"""Token generation, hashing, and prefix helpers.

The format ``a2x_pat_<43-char base64url>`` mirrors GitHub PAT / PyPI token
conventions: a fixed prefix makes secret scanners (gitleaks, trufflehog)
and log-redaction regexes trivial to write. The base64url body is 32 bytes
of OS-provided randomness → 256-bit entropy, well past any in-line brute
force threat for a server-side API key.

Storage rule: this module produces plaintext exactly once. Anything that
persists must do so as ``hash_token(plaintext)``. There is intentionally
no ``unhash`` direction.
"""

from __future__ import annotations

import hashlib
import hmac
import secrets

TOKEN_PREFIX = "a2x_pat_"
# Display prefix length: 4 chars after the underscore is enough for human
# disambiguation in a UI while leaking essentially nothing about the rest.
DISPLAY_PREFIX_LEN = len(TOKEN_PREFIX) + 4   # → "a2x_pat_xxxx"


def generate_token() -> str:
    """Generate a fresh ``a2x_pat_<43-char>`` API key (plaintext, return once)."""
    return TOKEN_PREFIX + secrets.token_urlsafe(32)


def hash_token(token: str) -> str:
    """Return ``sha256(token)`` as lowercase hex. Stable across processes."""
    return hashlib.sha256(token.encode("utf-8")).hexdigest()


def token_prefix(token: str) -> str:
    """Return the short prefix used for UI display / audit log entries.

    Includes the ``a2x_pat_`` literal so search tools can find these
    strings in logs. Does NOT leak enough material to authenticate.
    """
    return token[:DISPLAY_PREFIX_LEN]


def constant_time_equals(a: str, b: str) -> bool:
    """Wrapper around ``hmac.compare_digest`` for string comparison.

    Used on the hot path to compare the inbound token's hash against the
    stored hash for the matching key_id. Constant-time even when lengths
    differ (returns False but doesn't short-circuit early-out timing).
    """
    return hmac.compare_digest(a, b)
