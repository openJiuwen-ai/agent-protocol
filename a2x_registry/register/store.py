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

from .models import AgentCard, GenericServiceData, RegistryEntry, SkillData

logger = logging.getLogger(__name__)

USER_CONFIG_FILE = "user_config.json"
API_CONFIG_FILE = "api_config.json"
REGISTER_CONFIG_FILE = "register_config.json"
VECTOR_CONFIG_FILE = "vector_config.json"
SERVICE_JSON_FILE = "service.json"
SKILLS_DIR = "skills"
REMOVED_SKILLS_DIR = "removed_skills"


class RegistryStore:
    """Thread-safe file I/O for a single dataset directory."""

    def __init__(self, dataset_dir: Path):
        self._dir = dataset_dir
        self._dir.mkdir(parents=True, exist_ok=True)
        self._api_entries: Dict[str, RegistryEntry] = {}
        self._lock = threading.Lock()

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
        _atomic_write(self._dir / API_CONFIG_FILE, {"services": services})

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
                name = meta["name"]
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

    def save_skill_zip(self, zip_bytes: bytes) -> SkillData:
        """Extract a skill ZIP, validate SKILL.md, save to skills/{name}/. Returns SkillData."""
        with zipfile.ZipFile(io.BytesIO(zip_bytes)) as zf:
            # Security: reject paths with .. or absolute paths
            for info in zf.infolist():
                if info.filename.startswith("/") or ".." in info.filename:
                    raise ValueError(f"Unsafe path in ZIP: {info.filename}")

            # Find SKILL.md — at root or inside a single top-level directory
            names = zf.namelist()
            skill_md_path = None
            strip_prefix = ""
            if "SKILL.md" in names:
                skill_md_path = "SKILL.md"
            else:
                # Check for single top-level directory containing SKILL.md
                top_dirs = {n.split("/")[0] for n in names if "/" in n}
                for td in top_dirs:
                    candidate = f"{td}/SKILL.md"
                    if candidate in names:
                        skill_md_path = candidate
                        strip_prefix = f"{td}/"
                        break
            if not skill_md_path:
                raise ValueError("ZIP must contain SKILL.md (at root or in a single top-level directory)")

            # Parse frontmatter
            skill_md_content = zf.read(skill_md_path).decode("utf-8")
            meta = parse_skill_md_content(skill_md_content)
            name = meta["name"]

            # Extract to skills/{name}/
            target_dir = self._dir / SKILLS_DIR / name
            if target_dir.exists():
                shutil.rmtree(target_dir)
            target_dir.mkdir(parents=True, exist_ok=True)

            for info in zf.infolist():
                if info.is_dir():
                    continue
                # Strip prefix if SKILL.md was inside a subdirectory
                rel_path = info.filename
                if strip_prefix and rel_path.startswith(strip_prefix):
                    rel_path = rel_path[len(strip_prefix):]
                if not rel_path:
                    continue
                dest = target_dir / rel_path
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
        skill_dir = self._dir / SKILLS_DIR / name
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
        old_dir = self._dir / SKILLS_DIR / old_name
        new_dir = self._dir / SKILLS_DIR / new_name
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
        skill_md = self._dir / SKILLS_DIR / name / "SKILL.md"
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
        """Pack a skill folder into an in-memory ZIP. Raises FileNotFoundError if missing."""
        skill_dir = self._dir / SKILLS_DIR / name
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
        _atomic_write(self._dir / SERVICE_JSON_FILE, services)

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
        _atomic_write(self._dir / REGISTER_CONFIG_FILE, {"formats": formats})

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
        _atomic_write(
            self._dir / VECTOR_CONFIG_FILE,
            {"embedding_model": embedding_model, "embedding_dim": embedding_dim},
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
            agent_card=agent_card, agent_card_url=card_url,
        )
    return None


def _entry_to_config_dict(entry: RegistryEntry) -> dict:
    """Convert a RegistryEntry back to the config file dict format."""
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


def _atomic_write(path: Path, data):
    """Write JSON data atomically via temp file + os.replace."""
    content = json.dumps(data, ensure_ascii=False, indent=2) + "\n"
    tmp_path = path.with_suffix(".tmp")
    with open(tmp_path, "w", encoding="utf-8") as f:
        f.write(content)
    try:
        os.replace(str(tmp_path), str(path))
    except OSError:
        import time
        time.sleep(0.05)
        try:
            os.replace(str(tmp_path), str(path))
        except OSError:
            # Fallback: direct overwrite (non-atomic but functional on Windows)
            with open(path, "w", encoding="utf-8") as f:
                f.write(content)
            if tmp_path.exists():
                tmp_path.unlink(missing_ok=True)
