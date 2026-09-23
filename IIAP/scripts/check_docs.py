"""Check IIAP Markdown dates and local links."""

from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).parents[1]
docs = sorted((ROOT / "docs").rglob("*.md"))
errors: list[str] = []
link_pattern = re.compile(r"\[[^]]+\]\(([^)]+)\)")

for document in docs:
    text = document.read_text()
    if not re.search(r"^(?:最后更新：|Last updated: )\d{4}-\d{2}-\d{2}$", text, re.MULTILINE):
        errors.append(f"missing last-updated date: {document.relative_to(ROOT)}")
    for target in link_pattern.findall(text):
        clean = target.split("#", 1)[0]
        if not clean or "://" in clean or clean.startswith("mailto:"):
            continue
        if not (document.parent / clean).resolve().exists():
            errors.append(f"broken link in {document.relative_to(ROOT)}: {target}")

if errors:
    raise SystemExit("\n".join(errors))
print(f"checked {len(docs)} IIAP documents")
