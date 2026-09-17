"""Traditional (MCP-style) search: pass ALL service descriptions to LLM and let it select.

No taxonomy, no pre-filtering. The LLM sees the full service catalog in one prompt
and returns the IDs of matching services.

Interface consistent with A2XSearch:
    search(query) -> (List[SearchResult], SearchStats)
"""

import json
import re
from dataclasses import dataclass

from a2x_registry.common.llm_client import LLMClient, parse_json_response
from a2x_registry.common.models import SearchResult  # noqa: F401


@dataclass
class SearchStats:
    """Search statistics, matching A2X SearchStats interface."""
    llm_calls: int = 1
    total_tokens: int = 0


class TraditionalSearch:
    """Full-context LLM search: feed all service descriptions, let LLM pick.

    Interface consistent with A2XSearch:
        search(query) -> (List[SearchResult], SearchStats)

    Args:
        service_path: Path to service.json
    """

    def __init__(self, service_path: str = "database/ToolRet_clean/service.json"):
        self.llm = LLMClient()

        with open(service_path, 'r', encoding='utf-8') as f:
            self.services = json.load(f)

        self.service_map = {s["id"]: s for s in self.services}
        self.catalog_text = self._build_catalog()

    def _build_catalog(self) -> str:
        """Build a compact service catalog string for the prompt."""
        lines = []
        for s in self.services:
            lines.append(f'- [{s["id"]}] {s["name"]}: {s["description"]}')
        return "\n".join(lines)

    def search(self, query: str) -> tuple:
        """Search for services matching the query.

        Args:
            query: User query string

        Returns:
            (List[SearchResult], SearchStats)
        """
        system_prompt = (
            "You are a service discovery assistant. "
            "Given a user query and a catalog of available services, "
            "identify ALL services that could fulfill the user's request.\n\n"
            "IMPORTANT:\n"
            "- Return service IDs that are relevant to the query\n"
            "- Include all plausible matches, not just the single best one\n"
            "- Consider both exact matches and closely related services\n"
            "- Return ONLY a JSON object with a \"service_ids\" array\n"
        )

        user_prompt = (
            f"## Service Catalog ({len(self.services)} services)\n\n"
            f"{self.catalog_text}\n\n"
            f"## User Query\n\n"
            f"{query}\n\n"
            f"## Task\n\n"
            f"Return a JSON object with a \"service_ids\" array containing "
            f"the IDs of ALL services that could help fulfill this query.\n\n"
            f"```json\n"
            f"{{\"service_ids\": [\"id1\", \"id2\", ...]}}\n"
            f"```"
        )

        messages = [
            {"role": "system", "content": system_prompt},
            {"role": "user", "content": user_prompt},
        ]

        response = self.llm.call(messages, temperature=0.0)

        stats = SearchStats(
            llm_calls=1,
            total_tokens=response.tokens,
        )

        if not response.success:
            return [], stats

        parsed = parse_json_response(response.content)
        if parsed and "service_ids" in parsed:
            service_ids = parsed["service_ids"]
        else:
            service_ids = re.findall(r'["\']([a-zA-Z_]+_\d+)["\']', response.content)

        results = []
        for sid in service_ids:
            if sid in self.service_map:
                results.append(SearchResult(
                    id=sid,
                    name=self.service_map[sid]["name"],
                    description=self.service_map[sid].get("description", ""),
                ))

        return results, stats
