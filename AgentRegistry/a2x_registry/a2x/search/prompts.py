"""Prompt templates and response parsing for A2X search."""

from typing import List, Dict, Callable


def build_category_prompt(
    mode: str,
    query: str,
    child_ids: List[str],
    get_category_info: Callable[[str], Dict],
    parent_path: str = "",
) -> str:
    """Build LLM prompt for category selection."""
    category_list = []
    for i, cat_id in enumerate(child_ids):
        info = get_category_info(cat_id)
        name = info.get("name", cat_id)
        desc = info.get("description", "No description")
        boundary = info.get("boundary", "")
        if mode == "get_one" and boundary:
            category_list.append(f"{i+1}. {name}: {desc} [{boundary}]")
        else:
            category_list.append(f"{i+1}. {name}: {desc}")

    categories_text = "\n".join(category_list)

    if mode == "get_one":
        path_hint = f"\nCurrent path: {parent_path}" if parent_path else ""
        return f"""Think about what specific service or tool the user needs, then select the ONE category where such a service would be classified. Match by service function, not query topic.
{path_hint}

Query: {query}

Categories:
{categories_text}

Return ONLY one number (e.g. "3"), or "NONE" if no category is relevant."""

    if mode == "get_important":
        return f"""Analyze the user's request step by step: identify each distinct action or need, then select the categories that would contain services for those actions. Include a category if any part of the request could require it. Exclude categories that have no functional connection to any part of the request.

Query: {query}

Categories:
{categories_text}

Return ONLY the numbers separated by commas (e.g. "1,3,5"), or "NONE"."""

    # get_all (default)
    return f"""Select ALL categories that could contain relevant services for the query. Think about what actions and entities the user needs, then match to categories by keyword and semantic similarity. Include ALL potentially relevant categories — when in doubt, include it.

Query: {query}

Categories:
{categories_text}

Return ONLY the numbers separated by commas (e.g. "1,3,5"), or "NONE"."""


def build_service_prompt(
    mode: str,
    query: str,
    service_ids: List[str],
    get_service_info: Callable[[str], Dict],
) -> str:
    """Build LLM prompt for service selection."""
    service_list = []
    for i, svc_id in enumerate(service_ids):
        svc = get_service_info(svc_id)
        name = svc.get("name", "Unknown")
        desc = svc.get("description", "No description")
        service_list.append(f"{i+1}. {name}: {desc}")

    services_text = "\n".join(service_list)

    if mode == "get_one":
        return f"""Select the ONE service that most directly fulfills the user's primary need. Match the specific action described in the query to the service's described functionality.

Query: {query}

Services:
{services_text}

Return ONLY one number (e.g. "3"), or "NONE" if no service is relevant."""

    if mode == "get_important":
        return f"""Select services the user clearly needs to fulfill their request. A service should be included if the query explicitly requires its functionality. If two services do the same thing, include only the better match. Exclude services that are not directly requested.

Query: {query}

Services:
{services_text}

Return ONLY the numbers separated by commas (e.g. "1,3,5"), or "NONE"."""

    # get_all (default)
    return f"""Select ALL services that could help fulfill the query. Include related and prerequisite services. When uncertain, include it.

Query: {query}

Services:
{services_text}

Return ONLY the numbers separated by commas (e.g. "1,3,5"), or "NONE"."""


def parse_selection(response: str, max_index: int, mode: str) -> List[int]:
    """Parse LLM response to extract selected indices (0-based)."""
    response = response.strip().upper()

    if response == "NONE" or not response:
        return []

    indices = []
    parts = response.replace(",", " ").split()
    for part in parts:
        try:
            num = int(part.strip())
            if 1 <= num <= max_index:
                indices.append(num - 1)
        except ValueError:
            continue

    if mode == "get_one" and len(indices) > 1:
        indices = indices[:1]

    return indices
