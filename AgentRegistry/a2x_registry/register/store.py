"""RegistryStore — per-dataset file I/O manager.

Manages user_config.json (read-only) + api_config.json (read-write) + service.json (write-only).
Keeps api_config in memory to avoid re-reading on every write.
"""

import hashlib
import io
import json
import logging
import os
import re
import shutil
import threading
import zipfile
from pathlib import Path
from typing import Any, Dict, List, Optional

from a2x_registry.common.atomic import atomic_write_json

from .models import AgentCard, GenericServiceData, RegistryEntry, SkillData

logger = logging.getLogger(__name__)

USER_CONFIG_FILE = "user_config.json"
API_CONFIG_FILE = "api_config.json"
REGISTER_CONFIG_FILE = "register_config.json"
VECTOR_CONFIG_FILE = "vector_config.json"
AUTH_CONFIG_FILE = "auth_config.json"
LEASE_CONFIG_FILE = "lease_config.json"
SERVICE_JSON_FILE = "service.json"
SKILLS_DIR = "skills"
REMOVED_SKILLS_DIR = "removed_skills"

# ---------------------------------------------------------------------------
# Skill name validation
#
# A skill name comes from user-supplied content: the ``name`` key of a
# SKILL.md frontmatter (uploaded as a ZIP) or the ``name`` field of a service
# update. It becomes a directory under ``<dataset>/skills/``, so an unvalidated
# ``..`` makes ``shutil.rmtree(<dataset>/skills/..)`` delete the whole dataset.
# The ZIP entry paths are guarded separately in ``_validate_zip_entry``.
# ---------------------------------------------------------------------------
_MAX_SKILL_NAME_BYTES = 200
_WINDOWS_RESERVED_NAMES = {
    "con", "prn", "aux", "nul",
    *{f"com{i}" for i in range(1, 10)},
    *{f"lpt{i}" for i in range(1, 10)},
}


def validate_skill_name(name: str) -> str:
    """Return ``name`` if it is a safe skill directory name, else raise ValueError.

    Rules that only matter on Windows (reserved device names, ``:``, leading /
    trailing whitespace, trailing dot) are enforced on Windows only: on POSIX
    those names are legal and skills created by older versions must stay
    readable and deletable. Control characters are rejected everywhere — they
    would desync the folder name from the frontmatter it was parsed from.
    """
    if not isinstance(name, str) or not name:
        raise ValueError("skill name must be a non-empty string")
    if name in (".", ".."):
        raise ValueError(f"invalid skill name: {name!r}")
    if len(name.encode("utf-8")) > _MAX_SKILL_NAME_BYTES:
        raise ValueError(
            f"invalid skill name: too long ({len(name.encode('utf-8'))} bytes > "
            f"{_MAX_SKILL_NAME_BYTES})"
        )
    for ch in ("/", "\\", "\x00"):
        if ch in name:
            raise ValueError(f"invalid skill name: path separator or NUL in {name!r}")
    if any(ord(ch) < 32 or ord(ch) == 127 for ch in name):
        raise ValueError(f"invalid skill name: control character in {name!r}")
    if os.name == "nt":
        if name.strip() != name:
            raise ValueError(f"invalid skill name: leading/trailing whitespace: {name!r}")
        if ":" in name:
            raise ValueError(f"invalid skill name: ':' is not allowed in {name!r}")
        if name != name.rstrip(" ."):
            raise ValueError(
                f"invalid skill name: trailing dot/space aliases another directory: {name!r}"
            )
        if name.split(".")[0].lower() in _WINDOWS_RESERVED_NAMES:
            raise ValueError(f"invalid skill name: {name!r} is a reserved device name")
    return name


def _validate_zip_entry(entry_name: str) -> None:
    """Raise ValueError for a ZIP entry that could escape the extraction root.

    ZIP entry names are POSIX-style by specification, whatever the extracting
    host, so they are parsed as POSIX here and every component is checked. A
    Windows drive prefix (``C:``) and a backslash are not POSIX absolute paths
    but *are* absolute / a separator once extracted on Windows, so both are
    rejected explicitly instead of relying on the host separator.

    Call this with ``ZipInfo.orig_filename``, never with ``ZipInfo.filename``:
    ``zipfile`` truncates ``filename`` at the first NUL (``orig_filename`` keeps
    it), so a NUL check on ``filename`` can never fire, and a name that only
    differs in its truncated part would slip through.
    """
    if not entry_name:
        raise ValueError("Unsafe path in ZIP: empty entry name")
    if "\x00" in entry_name:
        raise ValueError(f"Unsafe path in ZIP: NUL in {entry_name!r}")
    if "\\" in entry_name:
        raise ValueError(f"Unsafe path in ZIP: backslash in {entry_name!r}")
    if entry_name.startswith("/"):
        raise ValueError(f"Unsafe path in ZIP: absolute path {entry_name!r}")
    if re.match(r"^[A-Za-z]:", entry_name):
        raise ValueError(f"Unsafe path in ZIP: drive prefix in {entry_name!r}")
    for part in entry_name.split("/"):
        if part == "..":
            raise ValueError(f"Unsafe path in ZIP: parent reference in {entry_name!r}")


def _skill_names_match(frontmatter_name: str, folder_name: str) -> bool:
    """Does a frontmatter ``name`` refer to the folder it was read from?

    Windows filesystems are case-insensitive: ``skills/My-Skill`` and a
    frontmatter ``name: my-skill`` are the same directory, so an exact match
    would silently drop skills that were valid before this check landed (they
    disappear from the registry, and from download/pack too). POSIX stays
    exact — ``My-Skill`` and ``my-skill`` are two different folders there.
    """
    if frontmatter_name == folder_name:
        return True
    return os.name == "nt" and frontmatter_name.lower() == folder_name.lower()


def _locate_skill_md(zf: zipfile.ZipFile) -> "tuple[str, str]":
    """Return ``(skill_md_entry, strip_prefix)`` for a skill ZIP.

    Accepts SKILL.md at the archive root, or inside a single top-level
    directory (``my-skill/SKILL.md`` — the layout the AgentSkills spec allows).
    ``strip_prefix`` is what has to be removed from entry names on extraction.
    """
    names = zf.namelist()
    if "SKILL.md" in names:
        return "SKILL.md", ""
    for top in sorted({n.split("/")[0] for n in names if "/" in n}):
        candidate = f"{top}/SKILL.md"
        if candidate in names:
            return candidate, f"{top}/"
    raise ValueError("ZIP must contain SKILL.md (at root or in a single top-level directory)")



# Default behavior when ``auth_config.json`` is missing: the namespace is
# anonymous (backward-compat with every dataset created before this module
# existed). Setting ``required: true`` is opt-in per dataset.
AUTH_CONFIG_DEFAULT: Dict[str, Any] = {"required": False, "schema_version": 1}

# Default when ``lease_config.json`` is missing: heartbeat unsupported on
# this namespace — every registration is permanent (legacy behavior). Admin
# opts in by writing ``{"enabled": true, "min_ttl": ..., "max_ttl": ...,
# "grace_period": ...}``. See docs/heartbeat_design.md.
LEASE_CONFIG_DEFAULT: Dict[str, Any] = {
    "enabled": False,
    "min_ttl": 10,
    "max_ttl": 3600,
    "grace_period": 300,
    "schema_version": 1,
}


class RegistryStore:
    """Thread-safe file I/O for a single dataset directory."""

    def __init__(self, dataset_dir: Path):
        self._dir = dataset_dir
        self._dir.mkdir(parents=True, exist_ok=True)
        self._api_entries: Dict[str, RegistryEntry] = {}
        self._lock = threading.Lock()

    # -- skill directory helpers -------------------------------------------

    def _skill_dir(self, name: str) -> Path:
        """Return ``<dataset>/skills/<name>`` after validating ``name``."""
        validate_skill_name(name)
        return self._dir / SKILLS_DIR / name

    def _skills_root(self) -> Path:
        return self._dir / SKILLS_DIR

    def _safe_rmtree(self, target: Path) -> None:
        """Delete a skill folder, refusing anything outside ``skills/``."""
        root = self._skills_root().resolve()
        resolved = target.resolve()
        if resolved == root or root not in resolved.parents:
            raise ValueError(f"refusing to delete outside {SKILLS_DIR}/: {resolved}")
        shutil.rmtree(resolved)

    # --- Read ---

    def load_user_config(self) -> List[RegistryEntry]:
        """Read user_config.json (user-maintained, system read-only)."""
        path = self._dir / USER_CONFIG_FILE
        if not path.exists():
            return []
        entries = _load_config_file(path, source="user_config")
        if not entries and path.stat().st_size > 10:
            logger.error("user_config.json exists but produced 0 entries: %s", path)
        return entries

    def load_api_config(self) -> List[RegistryEntry]:
        """Read api_config.json and cache in memory."""
        path = self._dir / API_CONFIG_FILE
        entries = _load_config_file(path, source="api_config")
        with self._lock:
            self._api_entries = {e.service_id: e for e in entries}
        return entries

    # --- Write api_config ---

    def save_api_entry(self, entry: RegistryEntry):
        """Upsert one entry into api_config.json (memory + disk)."""
        with self._lock:
            self._api_entries[entry.service_id] = entry
            self._flush_api_config()

    def remove_api_entry(self, service_id: str) -> bool:
        """Remove one entry from api_config.json. Returns True if found."""
        with self._lock:
            if service_id not in self._api_entries:
                return False
            del self._api_entries[service_id]
            self._flush_api_config()
            return True

    def save_api_batch(self, entries: List[RegistryEntry]):
        """Replace all api_config entries and write to disk."""
        with self._lock:
            self._api_entries = {e.service_id: e for e in entries}
            self._flush_api_config()

    def _flush_api_config(self):
        """Serialize _api_entries to api_config.json. Must be called with lock held."""
        services = [_entry_to_config_dict(e) for e in self._api_entries.values()]
        atomic_write_json(self._dir / API_CONFIG_FILE, {"services": services})

    # --- Skill folder I/O ---

    def load_skills(self) -> List[RegistryEntry]:
        """Scan skills/*/SKILL.md and return RegistryEntry for each valid skill."""
        skills_dir = self._dir / SKILLS_DIR
        if not skills_dir.is_dir():
            return []
        entries = []
        for child in sorted(skills_dir.iterdir()):
            skill_md = child / "SKILL.md"
            if not child.is_dir() or not skill_md.exists():
                continue
            try:
                meta = parse_skill_md(skill_md)
                name = validate_skill_name(meta["name"])
                if not _skill_names_match(name, child.name):
                    # A frontmatter name that differs from the folder name would
                    # make skill_path / get_skill_zip address a different
                    # directory than the one on disk. Treat it as corrupt data.
                    # Windows compares case-insensitively — see
                    # _skill_names_match for why an exact match would hide
                    # existing skills there.
                    raise ValueError(
                        f"frontmatter name {name!r} does not match folder {child.name!r}"
                    )
                skill_data = SkillData(
                    name=name,
                    description=meta["description"],
                    license=meta.get("license", ""),
                    skill_path=f"{SKILLS_DIR}/{name}",
                    files=_list_skill_files(child),
                )
                entries.append(RegistryEntry(
                    service_id=generate_service_id("skill", name),
                    type="skill",
                    source="skill_folder",
                    skill_data=skill_data,
                ))
            except Exception as e:
                logger.warning("Skipping invalid skill '%s': %s", child.name, e)
        return entries

    def peek_skill_name(self, zip_bytes: bytes) -> str:
        """Validate a skill ZIP and return its frontmatter name — no writes.

        ``save_skill_zip`` replaces ``skills/<name>/`` (no backup), so a caller
        that has to refuse the upload for a reason the *store* doesn't know
        about — e.g. the ownership check in
        ``RegistryService.register_skill`` — must learn the name first.
        Validation is identical to ``save_skill_zip``: a ZIP rejected here is
        rejected there too.
        """
        with zipfile.ZipFile(io.BytesIO(zip_bytes)) as zf:
            for info in zf.infolist():
                _validate_zip_entry(info.orig_filename)
            skill_md_path, _ = _locate_skill_md(zf)
            skill_md_content = zf.read(skill_md_path).decode("utf-8")
        meta = parse_skill_md_content(skill_md_content)
        return validate_skill_name(meta["name"])

    def save_skill_zip(self, zip_bytes: bytes) -> SkillData:
        """Extract a skill ZIP, validate SKILL.md, save to skills/{name}/. Returns SkillData."""
        with zipfile.ZipFile(io.BytesIO(zip_bytes)) as zf:
            # Security: every entry must be a relative POSIX path without any
            # parent reference, backslash, drive prefix or NUL — see
            # _validate_zip_entry for why each of those is needed. Validated on
            # ``orig_filename``: ``filename`` is truncated at the first NUL.
            for info in zf.infolist():
                _validate_zip_entry(info.orig_filename)

            # Find SKILL.md — at root or inside a single top-level directory
            skill_md_path, strip_prefix = _locate_skill_md(zf)

            # Parse frontmatter
            skill_md_content = zf.read(skill_md_path).decode("utf-8")
            meta = parse_skill_md_content(skill_md_content)
            name = validate_skill_name(meta["name"])

            # Extract to skills/{name}/
            target_dir = self._skill_dir(name)
            if target_dir.exists():
                self._safe_rmtree(target_dir)
            target_dir.mkdir(parents=True, exist_ok=True)
            target_root = target_dir.resolve()

            for info in zf.infolist():
                if info.is_dir():
                    continue
                # Strip prefix if SKILL.md was inside a subdirectory.
                # ``orig_filename`` (not ``filename``) is the name that was
                # validated above — never extract from a name the checks didn't
                # see.
                rel_path = info.orig_filename
                if strip_prefix and rel_path.startswith(strip_prefix):
                    rel_path = rel_path[len(strip_prefix):]
                if not rel_path:
                    continue
                dest = target_dir / rel_path
                # Defence in depth: even with the entry checks above, verify the
                # resolved destination is still inside the skill directory
                # before touching the filesystem.
                resolved = dest.resolve()
                if resolved == target_root or target_root not in resolved.parents:
                    raise ValueError(
                        f"Unsafe path in ZIP: {info.filename!r} escapes the skill directory"
                    )
                dest.parent.mkdir(parents=True, exist_ok=True)
                with zf.open(info) as src, open(dest, "wb") as dst:
                    dst.write(src.read())

        return SkillData(
            name=name,
            description=meta["description"],
            license=meta.get("license", ""),
            skill_path=f"{SKILLS_DIR}/{name}",
            files=_list_skill_files(target_dir),
        )

    def remove_skill(self, name: str) -> bool:
        """Move a skill folder to removed_skills/. Returns True if it existed."""
        skill_dir = self._skill_dir(name)
        if not skill_dir.exists():
            return False
        removed_dir = self._dir / REMOVED_SKILLS_DIR
        removed_dir.mkdir(parents=True, exist_ok=True)
        dest = removed_dir / name
        if dest.exists():
            shutil.rmtree(dest)
        shutil.move(str(skill_dir), str(dest))
        return True

    def rename_skill(self, old_name: str, new_name: str) -> None:
        """Rename ``skills/{old_name}/`` to ``skills/{new_name}/``.

        Raises FileNotFoundError if source missing, ValueError if target exists.
        """
        if not new_name or new_name == old_name:
            return
        old_dir = self._skill_dir(old_name)
        new_dir = self._skill_dir(new_name)
        if not old_dir.exists():
            raise FileNotFoundError(f"Skill folder not found: {old_name}")
        if new_dir.exists():
            raise ValueError(f"Skill folder already exists: {new_name}")
        shutil.move(str(old_dir), str(new_dir))

    def update_skill_md(self, name: str, updates: Dict[str, str]) -> None:
        """Upsert frontmatter keys in ``skills/{name}/SKILL.md``.

        Preserves body text and any frontmatter keys not in ``updates``.
        Creates frontmatter if missing.
        """
        skill_md = self._skill_dir(name) / "SKILL.md"
        if not skill_md.exists():
            raise FileNotFoundError(f"SKILL.md not found for skill '{name}'")
        with open(skill_md, "r", encoding="utf-8") as f:
            content = f.read()

        parts = content.split("---", 2)
        if len(parts) < 3:
            raise ValueError("SKILL.md must have YAML frontmatter delimited by ---")
        fm_body = parts[1].strip("\n")
        body = parts[2]

        # Line-level upsert: rewrite existing key lines, append new ones.
        pattern = re.compile(r"^(\s*)([A-Za-z_][\w-]*)(\s*:\s*)(.*)$")
        new_lines: List[str] = []
        seen = set()
        for line in fm_body.split("\n"):
            m = pattern.match(line)
            if m and m.group(2) in updates:
                new_lines.append(f"{m.group(1)}{m.group(2)}{m.group(3)}{updates[m.group(2)]}")
                seen.add(m.group(2))
            else:
                new_lines.append(line)
        for k, v in updates.items():
            if k not in seen:
                new_lines.append(f"{k}: {v}")

        new_content = "---\n" + "\n".join(new_lines) + "\n---" + body
        with open(skill_md, "w", encoding="utf-8") as f:
            f.write(new_content)

    def get_skill_zip(self, name: str) -> bytes:
        """Pack a skill folder into an in-memory ZIP. Raises FileNotFoundError if missing.

        Goes through ``_skill_dir`` like every write path: ``..`` would
        otherwise resolve to the whole dataset directory and pack
        ``service.json`` / ``api_config.json`` / ``auth_config.json`` into the
        response.
        """
        skill_dir = self._skill_dir(name)
        if not skill_dir.is_dir():
            raise FileNotFoundError(f"Skill folder not found: {name}")
        buf = io.BytesIO()
        with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as zf:
            for root, _dirs, files in os.walk(skill_dir):
                for fname in files:
                    full = Path(root) / fname
                    arcname = full.relative_to(skill_dir).as_posix()
                    zf.write(full, arcname)
        return buf.getvalue()

    # --- Write service.json ---

    def write_service_json(self, services: List[dict]):
        """Atomically write the output service.json."""
        atomic_write_json(self._dir / SERVICE_JSON_FILE, services)

    # --- Register-format config ---

    def load_register_config(self) -> Optional[Dict[str, str]]:
        """Read ``register_config.json`` → ``{type: min_version}``.

        Returns ``None`` when the file is missing; caller substitutes defaults.
        Returns ``{}`` (empty dict) when the file exists but is malformed /
        declares no valid formats — caller should surface a hard ban on all
        types in that case.
        """
        path = self._dir / REGISTER_CONFIG_FILE
        if not path.exists():
            return None
        try:
            with open(path, "r", encoding="utf-8") as f:
                data = json.load(f)
        except (json.JSONDecodeError, OSError) as exc:
            logger.warning("Failed to load %s: %s", path, exc)
            return {}
        from .validation import normalize_format_config
        return normalize_format_config(data.get("formats"))

    def write_register_config(self, formats: Dict[str, str]) -> None:
        """Persist ``register_config.json`` with normalized formats dict."""
        atomic_write_json(self._dir / REGISTER_CONFIG_FILE, {"formats": formats})

    def load_vector_config(self) -> Optional[Dict[str, Any]]:
        """Read ``vector_config.json`` → ``{embedding_model, embedding_dim}``.

        Returns ``None`` if the file is missing or malformed (caller decides
        what default to apply).
        """
        path = self._dir / VECTOR_CONFIG_FILE
        if not path.exists():
            return None
        try:
            with open(path, encoding="utf-8") as f:
                data = json.load(f)
            if not isinstance(data, dict):
                return None
            return data
        except (OSError, json.JSONDecodeError):
            return None

    def write_vector_config(self, embedding_model: str, embedding_dim: int) -> None:
        """Persist ``vector_config.json`` atomically."""
        atomic_write_json(
            self._dir / VECTOR_CONFIG_FILE,
            {"embedding_model": embedding_model, "embedding_dim": embedding_dim},
        )

    # --- Auth config (per-namespace opt-in) ---

    def load_auth_config(self) -> Dict[str, Any]:
        """Read ``auth_config.json``; missing file → backward-compat default.

        Returns a dict at least containing ``required: bool``. The default
        when the file is absent is ``{"required": False, ...}`` so existing
        datasets (created before this field existed) behave as anonymous.
        Malformed file → log + default; never raises, never blocks startup.
        """
        path = self._dir / AUTH_CONFIG_FILE
        if not path.exists():
            return dict(AUTH_CONFIG_DEFAULT)
        try:
            with open(path, "r", encoding="utf-8") as f:
                data = json.load(f)
            if not isinstance(data, dict):
                logger.warning("Malformed %s (not a dict); falling back to default", path)
                return dict(AUTH_CONFIG_DEFAULT)
            required = bool(data.get("required", False))
            return {
                "required": required,
                "schema_version": int(data.get("schema_version", 1)),
            }
        except (OSError, json.JSONDecodeError, ValueError, TypeError) as exc:
            logger.warning("Failed to load %s: %s; falling back to default", path, exc)
            return dict(AUTH_CONFIG_DEFAULT)

    def write_auth_config(self, required: bool) -> None:
        """Persist ``auth_config.json`` with ``{"required": bool, ...}``."""
        atomic_write_json(
            self._dir / AUTH_CONFIG_FILE,
            {"required": bool(required), "schema_version": 1},
        )

    # --- Lease (heartbeat) config (per-namespace opt-in) ---

    def load_lease_config(self) -> Dict[str, Any]:
        """Read ``lease_config.json``; missing file → backward-compat default.

        Returns a dict with at least ``enabled: bool``. The default when the
        file is absent is ``{"enabled": False, ...}`` so existing datasets
        keep registering permanent services (no heartbeat). Malformed file
        → log + default; never raises (mirrors load_auth_config behavior).
        Loaded values are normalized: ints coerced, bounds clamped to >=1.
        """
        path = self._dir / LEASE_CONFIG_FILE
        if not path.exists():
            return dict(LEASE_CONFIG_DEFAULT)
        try:
            with open(path, "r", encoding="utf-8") as f:
                data = json.load(f)
            if not isinstance(data, dict):
                logger.warning("Malformed %s (not a dict); falling back to default", path)
                return dict(LEASE_CONFIG_DEFAULT)
            enabled = bool(data.get("enabled", False))
            # Coerce + clamp; reject silly values rather than letting them
            # poison the heartbeat store at registration time.
            min_ttl = max(1, int(data.get("min_ttl", LEASE_CONFIG_DEFAULT["min_ttl"])))
            max_ttl = max(min_ttl, int(data.get("max_ttl", LEASE_CONFIG_DEFAULT["max_ttl"])))
            grace = max(0, int(data.get("grace_period", LEASE_CONFIG_DEFAULT["grace_period"])))
            return {
                "enabled": enabled,
                "min_ttl": min_ttl,
                "max_ttl": max_ttl,
                "grace_period": grace,
                "schema_version": int(data.get("schema_version", 1)),
            }
        except (OSError, json.JSONDecodeError, ValueError, TypeError) as exc:
            logger.warning("Failed to load %s: %s; falling back to default", path, exc)
            return dict(LEASE_CONFIG_DEFAULT)

    def write_lease_config(
        self, enabled: bool, min_ttl: int, max_ttl: int, grace_period: int,
    ) -> None:
        """Persist ``lease_config.json``. Caller is responsible for bound sanity;
        load_lease_config will silently coerce, but writes assume validated input."""
        if min_ttl < 1:
            raise ValueError(f"min_ttl must be >= 1, got {min_ttl}")
        if max_ttl < min_ttl:
            raise ValueError(f"max_ttl ({max_ttl}) must be >= min_ttl ({min_ttl})")
        if grace_period < 0:
            raise ValueError(f"grace_period must be >= 0, got {grace_period}")
        atomic_write_json(
            self._dir / LEASE_CONFIG_FILE,
            {
                "enabled": bool(enabled),
                "min_ttl": int(min_ttl),
                "max_ttl": int(max_ttl),
                "grace_period": int(grace_period),
                "schema_version": 1,
            },
        )


# ---------------------------------------------------------------------------
# Module-level utilities (no instance state, reusable)
# ---------------------------------------------------------------------------

def generate_service_id(type_prefix: str, name: str) -> str:
    """Generate a deterministic service ID from type prefix and name.

    Uses 16 hex chars (64 bits) to avoid birthday-paradox collisions.
    """
    h = hashlib.sha256(name.encode()).hexdigest()[:16]
    return f"{type_prefix}_{h}"


def _load_config_file(path: Path, source: str) -> List[RegistryEntry]:
    """Parse a config file (user_config or api_config format) into entries."""
    if not path.exists():
        return []
    try:
        with open(path, "r", encoding="utf-8") as f:
            data = json.load(f)
    except (json.JSONDecodeError, OSError) as e:
        logger.warning("Failed to load %s: %s", path, e)
        return []

    entries = []
    for svc in data.get("services", []):
        try:
            entry = _parse_service_entry(svc, source)
            if entry:
                entries.append(entry)
        except Exception as e:
            logger.warning("Skipping invalid entry in %s: %s — %s", path, svc.get("service_id", "?"), e)
    return entries


def _parse_service_entry(svc: dict, source: str) -> Optional[RegistryEntry]:
    """Parse a single service dict from config into a RegistryEntry."""
    svc_type = svc.get("type", "generic")
    service_id = svc.get("service_id", "")
    owner_id = svc.get("owner_id") or None  # missing field → unclaimed/legacy
    # lease_ttl is persisted as int when the entry was registered with a
    # heartbeat lease; absent for permanent services (legacy + namespaces
    # without lease_config). Coerce defensively.
    raw_ttl = svc.get("lease_ttl")
    lease_ttl: Optional[int] = int(raw_ttl) if isinstance(raw_ttl, (int, float)) and int(raw_ttl) > 0 else None

    if svc_type == "generic":
        name = (svc.get("name") or "").strip()
        desc = (svc.get("description") or "").strip()
        if not name or not desc:
            return None
        if not service_id:
            service_id = generate_service_id("generic", name)
        return RegistryEntry(
            service_id=service_id,
            type="generic",
            source=source,
            owner_id=owner_id,
            lease_ttl=lease_ttl,
            service_data=GenericServiceData(
                name=name, description=desc,
                inputSchema=svc.get("inputSchema", {}),
                url=svc.get("url"),
            ),
        )
    elif svc_type == "a2a":
        card_data = svc.get("agent_card")
        card_url = svc.get("agent_card_url")
        agent_card = AgentCard(**card_data) if card_data else None
        if not service_id:
            name = agent_card.name if agent_card else (card_url or "unknown")
            service_id = generate_service_id("agent", name)
        return RegistryEntry(
            service_id=service_id, type="a2a", source=source,
            owner_id=owner_id,
            lease_ttl=lease_ttl,
            agent_card=agent_card, agent_card_url=card_url,
        )
    return None


def _entry_to_config_dict(entry: RegistryEntry) -> dict:
    """Convert a RegistryEntry back to the config file dict format.

    ``owner_id`` and ``lease_ttl`` are omit-when-None so pre-auth /
    pre-heartbeat ``api_config.json`` files stay byte-equal after a round-
    trip (the backward-compat lockfile tests rely on this).
    """
    d: dict = {"type": entry.type, "service_id": entry.service_id}
    if entry.type == "generic" and entry.service_data:
        d["name"] = entry.service_data.name
        d["description"] = entry.service_data.description
        if entry.service_data.inputSchema:
            d["inputSchema"] = entry.service_data.inputSchema
        if entry.service_data.url:
            d["url"] = entry.service_data.url
    elif entry.type == "a2a":
        if entry.agent_card_url:
            d["agent_card_url"] = entry.agent_card_url
        if entry.agent_card:
            d["agent_card"] = entry.agent_card.model_dump(exclude_defaults=True)
    if entry.owner_id:
        d["owner_id"] = entry.owner_id
    if entry.lease_ttl:
        d["lease_ttl"] = int(entry.lease_ttl)
    return d


def parse_skill_md(path: Path) -> dict:
    """Parse SKILL.md file from disk. Returns {name, description, license}."""
    with open(path, "r", encoding="utf-8") as f:
        content = f.read()
    return parse_skill_md_content(content)


def parse_skill_md_content(content: str) -> dict:
    """Parse SKILL.md YAML frontmatter from string content.

    Expects --- delimited frontmatter with key: value lines.
    Returns {name, description, license}. Raises ValueError if name or description missing.
    """
    parts = content.split("---", 2)
    if len(parts) < 3:
        raise ValueError("SKILL.md must have YAML frontmatter delimited by ---")
    frontmatter = parts[1].strip()

    result = {}
    for line in frontmatter.splitlines():
        m = re.match(r"^(name|description|license):\s*(.+)$", line.strip())
        if m:
            val = m.group(2).strip()
            # Strip surrounding quotes (YAML quoted strings)
            if len(val) >= 2 and val[0] in ('"', "'") and val[-1] == val[0]:
                val = val[1:-1]
            result[m.group(1)] = val

    if not result.get("name"):
        raise ValueError("SKILL.md frontmatter must include 'name'")
    if not result.get("description"):
        raise ValueError("SKILL.md frontmatter must include 'description'")
    return result


def _list_skill_files(skill_dir: Path) -> List[str]:
    """List all files in a skill folder as relative POSIX paths."""
    files = []
    for root, _dirs, fnames in os.walk(skill_dir):
        for fname in fnames:
            full = Path(root) / fname
            files.append(full.relative_to(skill_dir).as_posix())
    return sorted(files)
