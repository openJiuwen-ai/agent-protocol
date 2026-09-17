"""Prompt templates, formatting helpers, and shared dataclasses for auto-hierarchical taxonomy building.

Supports both keyword-based (large nodes) and description-based (small nodes) category design.
Node context is included for non-root nodes to scope keyword extraction and category design.
"""

from dataclasses import dataclass, field
from typing import Dict, List, Optional


# =============================================================================
# Shared Dataclasses
# =============================================================================

@dataclass
class ClassificationResult:
    """Result of classifying a single service."""
    service_id: str
    category_ids: List[str] = field(default_factory=list)
    reasoning: str = ""
    success: bool = True
    error: Optional[str] = None


@dataclass
class NodeSplitResult:
    """Result of splitting a single node."""
    node_id: str
    subcategories: Dict[str, dict]        # sub_id -> {name, description, boundary, decision_rule}
    assignments: Dict[str, dict]          # svc_id -> {category_ids, reasoning}
    unclassified_service_ids: List[str]   # Services that remain at the parent node
    iterations_used: int = 0
    converged: bool = False


# =============================================================================
# Formatting Helpers
# =============================================================================

def format_services_for_prompt(services: List[Dict], max_desc_len: int = 150) -> str:
    """Format services as 'name: description' lines."""
    lines = []
    for svc in services:
        desc = svc.get('description', 'No description')
        if len(desc) > max_desc_len:
            desc = desc[:max_desc_len] + "..."
        lines.append(f"- {svc['name']}: {desc}")
    return "\n".join(lines)


def format_categories_for_prompt(categories: Dict[str, dict]) -> str:
    """Format categories for classification prompt."""
    lines = []
    for cat_id, info in sorted(categories.items()):
        lines.append(f"{cat_id}: {info['name']}")
        lines.append(f"  Description: {info['description']}")
        if info.get('decision_rule'):
            lines.append(f"  Decision Rule: {info['decision_rule']}")
        if info.get('boundary'):
            lines.append(f"  NOT here: {info['boundary']}")
        lines.append("")
    return "\n".join(lines)


def format_parent_boundary_section(parent_info: dict) -> str:
    """Format parent boundary info if available."""
    boundary = parent_info.get('boundary', '')
    if boundary:
        return f"Parent boundary: {boundary}\n"
    return ""


def format_subcategory_stats(
    subcategories: Dict[str, dict],
    assignments: Dict[str, dict],
) -> str:
    """Format per-subcategory service count."""
    cat_counts: Dict[str, int] = {}

    for svc_id, result in assignments.items():
        for cat_id in result.get('category_ids', []):
            cat_counts[cat_id] = cat_counts.get(cat_id, 0) + 1

    lines = []
    for cat_id, info in sorted(subcategories.items()):
        count = cat_counts.get(cat_id, 0)
        lines.append(f"  {cat_id} ({info['name']}): {count} services")
    return "\n".join(lines)


# =============================================================================
# Step 1: Keyword Extraction
# =============================================================================

SYSTEM_KEYWORD_EXTRACTION = (
    "You are an expert at identifying which USER DOMAIN an API service belongs to. "
    "You extract keywords that describe the FUNCTIONAL DOMAIN — the real-world field or area of life "
    "that the end user cares about when using this service."
)

KEYWORD_EXTRACTION_TEMPLATE = """Extract functional domain keywords for each of these {batch_size} API services.
{node_context_section}
RULES:
1. For each service, extract 1-{max_keywords} keywords describing the USER DOMAIN it serves — the real-world field or area of life the end user cares about.
2. Keywords must name the DOMAIN/FIELD, not the technical operation:
   GOOD: "weather", "stock_market", "restaurant", "email", "flight", "news", "sports", "music", "real_estate"
   BAD: "data_query", "api", "service", "tool", "information", "search", "conversion", "processing", "analytics" (these describe HOW, not WHAT DOMAIN)
   IMPORTANT: "weather" is better than "weather_data_query"; "travel" is better than "travel_search"; "finance" is better than "financial_analytics"
3. Use snake_case, 1-2 words per keyword. Prefer single domain words.
4. REUSE existing keywords from the list below when they fit — do NOT create synonyms.
   For example, if "weather" already exists, do NOT create "weather_forecast" or "weather_data".
   Only create a NEW keyword when no existing keyword adequately describes the service's domain.
5. Focus on the END USER'S PERSPECTIVE: What domain does a person care about when they use this service?
   - A weather API → keyword: "weather" (the user cares about weather, not about "data retrieval")
   - A flight search API → keyword: "travel" or "flight" (the user wants to travel, not to "search")
   - A stock price API → keyword: "stock_market" or "finance" (the user cares about finance)
   - An image recognition API → keyword based on its APPLICATION domain (e.g., "security" for face recognition, "healthcare" for medical imaging), not "image_processing"

EXISTING KEYWORDS (reuse when applicable):
{existing_keywords_text}

SERVICES:
{services_text}

Output ONLY valid JSON:
```json
{{
  "extractions": [
    {{"service_id": "...", "keywords": ["keyword1", "keyword2"]}}
  ]
}}
```"""


# =============================================================================
# Category Design from Keywords (large nodes)
# =============================================================================

SYSTEM_CATEGORY_DESIGN = (
    "You are an expert at designing functional taxonomy systems for API service registries. "
    "You create categories with CLEAR, NON-OVERLAPPING boundaries — like hospital departments "
    "where each patient knows exactly which department to go to."
)

CATEGORY_DESIGN_TEMPLATE = """Below are {n_keywords} functional domain keywords extracted from {n_services} API services,
with their frequency counts (how many services have this keyword):

{keywords_text}
{node_context_section}
Your task: Group these keywords into up to {max_cats} categories based on USER FUNCTIONAL DOMAIN.

CRITICAL RULES:
1. **USER FUNCTIONAL DOMAIN ONLY**: Each category must represent a real-world domain that end users care about.
   ALLOWED: "Travel & Tourism", "Finance & Banking", "Healthcare", "Food & Dining", "Sports", "News & Media", "Weather & Climate", "Entertainment", "Education", "Real Estate", "Automotive", "Communication"
   FORBIDDEN — Technical approach: "Developer Tools", "AI & Machine Learning", "Cloud Services", "Blockchain & Crypto Technology", "API Tools"
   FORBIDDEN — Data type: "Image Processing", "Audio Processing", "Video Services", "Text Analysis", "Data & Utilities"
   FORBIDDEN — Operation method: "Data Query", "Search Services", "Data Conversion", "Validation Services", "Analytics"
   FORBIDDEN — Disguised catch-all: "Data & Utilities", "Information Services", "Technical Services", "Digital Services", "General Tools" — any category whose services lack a common user scenario

2. **CLEAR BOUNDARIES**: Each category must have an explicit boundary statement pointing to other categories

3. **ABSOLUTELY NO CATCH-ALL**: Do NOT create any category that serves as a dumping ground.
   Test: Can you describe a SPECIFIC user scenario that connects ALL services in this category?
   - "I want to book a flight" → Travel ✓ (all travel services share this user context)
   - "I need domain-agnostic data processing" → NO USER SAYS THIS ✗ (catch-all in disguise)
   If a category's description uses words like "domain-agnostic", "general-purpose", "various", "miscellaneous", "cross-domain", it is a catch-all. Hard-to-classify services should be placed into their CLOSEST functional domain, not into a new generic category.

4. **PROTECT SMALL DOMAINS**: Do NOT merge small but distinct domains into larger ones.
   Even if only a few services exist for "Weather", "Astronomy", or "Pets", they deserve their own category if they represent a distinct user need. It is better to have a small category (5-10 services) than to lose it by merging into an unrelated domain.

5. **COMPLETE COVERAGE**: Every keyword must be assignable to at least one category.

For each category provide:
- id: "{parent_id}_sub1", "{parent_id}_sub2", etc.
- name: 2-4 word descriptive name
- description: What services belong here — positive definition, under 200 chars
- boundary: What does NOT belong here — point to other categories
- decision_rule: "If the user wants to [specific user goal], classify here"
- associated_keywords: Which input keywords map to this category

Output ONLY valid JSON:
```json
{{
  "dimension": "functional domain",
  "categories": [
    {{
      "id": "{parent_id}_sub1",
      "name": "...",
      "description": "...",
      "boundary": "...",
      "decision_rule": "...",
      "associated_keywords": ["keyword1", "keyword2"]
    }}
  ]
}}
```"""


# =============================================================================
# Root Category Validation (LLM-based)
# =============================================================================

SYSTEM_VALIDATE_ROOT_CATEGORIES = (
    "You are a strict taxonomy quality auditor. You identify categories that violate "
    "the functional-domain-only constraint for top-level API service classification."
)

VALIDATE_ROOT_CATEGORIES_TEMPLATE = """Review these top-level categories for an API service registry.

CATEGORIES:
{categories_text}

CHECK EACH CATEGORY against these STRICT rules:

1. **Must be a USER FUNCTIONAL DOMAIN**: The category must represent a real-world area of life that end users care about (e.g., Travel, Finance, Healthcare, Food, Sports).
   VIOLATIONS: categories based on technology (AI, Blockchain, Developer Tools), data type (Image/Audio/Video Processing), or operation (Data Query, Analytics, Conversion).

2. **Must NOT be a catch-all**: Every service in the category must share a concrete user scenario.
   VIOLATIONS: descriptions containing "domain-agnostic", "general-purpose", "various", or categories that are essentially "everything else".

3. **Must NOT overlap significantly**: If two categories cover substantially the same user domain, one should be merged or refined.

For each category, output:
- "valid": true/false
- "violation_type": null or one of "technical_approach", "data_type", "operation_method", "catch_all", "overlap"
- "reason": brief explanation if invalid

Output ONLY valid JSON:
```json
{{
  "validations": [
    {{"id": "cat_X", "valid": true, "violation_type": null, "reason": null}},
    {{"id": "cat_Y", "valid": false, "violation_type": "catch_all", "reason": "Description says domain-agnostic"}}
  ]
}}
```"""

REDESIGN_VIOLATED_CATEGORIES_TEMPLATE = """You designed top-level categories but some VIOLATED the functional-domain-only rule.

ALL CURRENT CATEGORIES:
{all_categories_text}

VIOLATED CATEGORIES (must be fixed):
{violated_categories_text}

KEYWORDS FROM VIOLATED CATEGORIES:
{violated_keywords_text}

YOUR TASK: Redistribute the keywords from violated categories into proper functional domain categories.

RULES:
1. You MUST REMOVE every violated category listed above.
2. For each keyword from violated categories:
   - If it fits an existing valid category, assign it there.
   - If multiple keywords share a distinct user functional domain NOT covered by existing categories, create a NEW functional domain category for them.
   - If a keyword represents a very niche domain, it's OK to create a small category (even for just 5-10 services).
3. Keep all existing VALID categories unchanged (same id, name, description, boundary, decision_rule).
4. Any new categories must be USER FUNCTIONAL DOMAINS (not technical/data-type/operation categories).
5. Do NOT create catch-all categories to absorb leftover keywords.

Output the COMPLETE updated category list (valid + new):
```json
{{
  "dimension": "functional domain",
  "categories": [
    {{
      "id": "...",
      "name": "...",
      "description": "...",
      "boundary": "...",
      "decision_rule": "...",
      "associated_keywords": ["keyword1", "keyword2"]
    }}
  ]
}}
```"""


# =============================================================================
# Category Design from Service Descriptions (small nodes)
# =============================================================================

SYSTEM_DESIGN_FROM_DESCRIPTIONS = (
    "You are an expert at subdividing a functional domain into clear, "
    "non-overlapping sub-categories for API service classification."
)

DESIGN_FROM_DESCRIPTIONS_TEMPLATE = """You are designing categories for a group of {service_count} API services.
{node_context_section}
Design up to {max_cats} categories with CLEAR, NON-OVERLAPPING boundaries.

MANDATORY RULES:
1. **SINGLE CLASSIFICATION DIMENSION**: All categories MUST use the SAME classification dimension.
   Priority order (use the highest applicable):
   a) Functional domain (e.g., Travel, Finance, Food) - PREFERRED
   b) Data/content type (e.g., Image Processing, Audio Processing) - if functional domain doesn't differentiate
   c) Operation type (e.g., Data Query, Data Conversion) - last resort
   GOOD: All by entity type (Stocks, Crypto, Forex) OR all by use-case (Analysis, Trading, Monitoring)
   BAD: Mixing entity (Stocks) with use-case (Analysis) at the same level
2. **NO TECHNICAL CATEGORIES**: Do NOT create AI, Machine Learning, Cloud, or API-related categories
3. **NO CATCH-ALL**: Do NOT create "Other", "General", "Miscellaneous" categories
4. **CONCISE DESCRIPTIONS**: Each description should be 1-2 sentences (under 200 characters)
5. **NON-OVERLAPPING**: Every pair of categories must have a clear distinguishing criterion

For each category provide:
- id: "{parent_id}_sub1", "{parent_id}_sub2", etc.
- name: 2-4 word descriptive name
- description: What services belong here (positive definition, under 200 chars)
- boundary: What does NOT belong here (pointing to sibling categories)
- decision_rule: "If the service primarily does X, classify here"

Services (name: description):
{services_text}

Output JSON:
```json
{{
  "dimension_used": "brief description of classification dimension chosen",
  "categories": [
    {{
      "id": "{parent_id}_sub1",
      "name": "...",
      "description": "...",
      "boundary": "...",
      "decision_rule": "..."
    }}
  ]
}}
```"""


# =============================================================================
# Formatting Helpers
# =============================================================================

def format_node_context_for_keywords(node_info: Optional[dict]) -> str:
    """Format node context section for keyword extraction prompt.

    For root nodes (node_info=None), returns empty string.
    For non-root nodes, returns context about the parent category to scope keyword extraction.
    """
    if not node_info:
        return ""
    name = node_info.get('name', 'Unknown')
    description = node_info.get('description', '')
    return (
        f"\nNODE CONTEXT:\n"
        f"These services belong to the category: \"{name}\"\n"
        f"Category description: {description}\n"
        f"Focus on keywords that distinguish services WITHIN this specific domain.\n"
    )


def format_node_context_for_design(node_info: Optional[dict], is_keyword_based: bool = False) -> str:
    """Format node context section for category design prompt.

    For root nodes (node_info=None), returns empty string.
    For non-root nodes, returns parent category context.
    """
    if not node_info:
        return ""
    name = node_info.get('name', 'Unknown')
    description = node_info.get('description', '')
    boundary = node_info.get('boundary', '')

    lines = [
        f"\nPARENT CATEGORY: \"{name}\"",
        f"Parent description: {description}",
    ]
    if boundary:
        lines.append(f"Parent boundary: {boundary}")
    lines.append("Design subcategories WITHIN this domain.\n")
    return "\n".join(lines)


def format_keywords_for_prompt(keywords: Dict[str, int], max_keywords: int = 300) -> str:
    """Format accumulated keywords for the extraction prompt."""
    if not keywords:
        return "(none yet — you are defining the initial keywords)"
    sorted_kw = sorted(keywords.items(), key=lambda x: -x[1])[:max_keywords]
    return "\n".join(f"- {kw} (count: {count})" for kw, count in sorted_kw)


def format_keywords_for_design(keywords: Dict[str, int]) -> str:
    """Format all keywords for the category design prompt."""
    sorted_kw = sorted(keywords.items(), key=lambda x: -x[1])
    return "\n".join(f"- {kw}: {count} services" for kw, count in sorted_kw)


def format_services_batch(services: List[Dict], max_desc_len: int = 150) -> str:
    """Format a batch of services for keyword extraction."""
    lines = []
    for svc in services:
        desc = svc.get('description', 'No description')
        if len(desc) > max_desc_len:
            desc = desc[:max_desc_len] + "..."
        lines.append(f"- [{svc['id']}] {svc['name']}: {desc}")
    return "\n".join(lines)


# =============================================================================
# Per-Service Classification within a Node
# =============================================================================

SYSTEM_CLASSIFY_NODE = (
    "You are a precise API service classifier. For each service, identify ALL relevant "
    "sub-categories based on functional domain."
)

CLASSIFY_SERVICE_IN_NODE_TEMPLATE = """Classify this API service into the sub-categories below.

PARENT CATEGORY: {parent_name}
{parent_description}

SUB-CATEGORIES:
{subcategories_text}

SERVICE DESCRIPTION:
{service_description}

INSTRUCTIONS:
1. List ALL sub-categories that are relevant to this service. If the service genuinely fits multiple sub-categories, list all of them.
2. If NO sub-category fits, return an empty list.

Output ONLY valid JSON:
```json
{{
  "reasoning": "brief explanation of classification logic",
  "category_ids": ["cat_X"]
}}
```"""


# =============================================================================
# Subcategory Refinement
# =============================================================================

SYSTEM_REFINE_NODE = (
    "You are an expert at refining sub-category definitions. You analyze classification "
    "feedback and make targeted adjustments to improve coverage and clarity."
)

REFINE_SUBCATEGORIES_TEMPLATE = """You defined sub-categories for "{parent_name}" but classification reveals problems.

CURRENT SUB-CATEGORIES ({n_subcategories}):
{current_subcategories_text}

CLASSIFICATION FEEDBACK:
- Total services: {n_total}
- Normal (1-{generic_threshold} categories): {n_normal}
- Generic (>{generic_threshold} categories, too many matches): {n_generic}
- Unclassified (0 categories): {n_unclassified}
- Per sub-category distribution:
{subcategory_stats_text}
{tiny_cats_text}

PROBLEMATIC SERVICES:
{problem_services_text}

YOUR TASK: Refine the sub-categories to reduce problematic services. You may:
1. SPLIT a large or vague sub-category into two clearer ones
2. MERGE two overlapping sub-categories to eliminate confusion
3. ADJUST descriptions/boundaries/decision_rules for clarity
4. ADD a new sub-category for services that don't fit existing ones

Focus on:
- GENERIC services (too many matches) → sharpen boundaries to make categories more distinctive
- UNCLASSIFIED services (no matches) → add new categories or broaden existing boundaries to cover them
- TINY sub-categories (too few services) → broaden boundaries to attract more services, or merge with similar categories

CONSTRAINTS:
- Keep up to {max_sub} sub-categories
- Do NOT create catch-all sub-categories ("Other", "General", "Miscellaneous", "Utilities", "Tools")
- Do NOT create disguised catch-all categories with "domain-agnostic", "general-purpose", or "various" in their description
- ALL sibling sub-categories must use the SAME classification dimension (do not mix functional with technical or data-type dimensions)
- PRESERVE existing sub-category IDs for unchanged/adjusted categories
- Only use NEW IDs for genuinely new sub-categories
- CONCISE DESCRIPTIONS: Each description MUST be under 200 characters.

Output the COMPLETE refined sub-category list:
```json
{{
  "changes_summary": "Brief description of what changed",
  "subcategories": [
    {{
      "id": "{parent_id}_sub1",
      "name": "...",
      "description": "...",
      "boundary": "...",
      "decision_rule": "..."
    }}
  ]
}}
```"""


def format_problem_services(
    services: List[Dict],
    assignments: Dict[str, dict],
    subcategories: Dict[str, dict],
    generic_ratio: float,
    max_samples: int = 50,
) -> str:
    """Format problematic services grouped by type for refinement prompt.

    Three types:
    1. GENERIC: matched > n_subcats * generic_ratio categories (too many matches)
    2. UNCLASSIFIED: 0 category_ids (no category fits)
    """
    svc_index = {s['id']: s for s in services}
    n_subcats = len(subcategories)
    threshold = max(1, int(n_subcats * generic_ratio))

    generic = []
    unclassified = []

    for svc_id, result in assignments.items():
        svc = svc_index.get(svc_id)
        if not svc:
            continue
        cat_ids = result.get('category_ids', [])
        n_cats = len(cat_ids)

        desc = svc.get('description', 'No description')
        if len(desc) > 150:
            desc = desc[:150] + "..."

        if n_cats == 0:
            unclassified.append(f"  - {desc}")
        elif n_cats > threshold:
            cats = ", ".join(cat_ids)
            generic.append(f"  - {desc} → matched: {cats}")

    if not generic and not unclassified:
        return "(No problematic services found)"

    sections = []
    if generic:
        sections.append(f"GENERIC (>{threshold} categories matched, boundaries too vague): {len(generic)} services")
        sections.extend(generic[:max_samples])
    if unclassified:
        sections.append(f"\nUNCLASSIFIED (0 categories, no match found): {len(unclassified)} services")
        sections.extend(unclassified[:max_samples])

    return "\n".join(sections)
