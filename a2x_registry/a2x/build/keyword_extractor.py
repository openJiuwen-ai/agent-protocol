"""Batched keyword extraction from services.

Pure computation — extracts keywords via LLM, returns results.
File I/O (caching) is handled by the caller (CategoryDesigner).
"""

import logging
from typing import Dict, List, Optional

from a2x_registry.common.llm_client import LLMClient, parse_json_response
from .config import AutoHierarchicalConfig
from .progress import progress_bar

logger = logging.getLogger(__name__)
from .prompts import (
    SYSTEM_KEYWORD_EXTRACTION,
    KEYWORD_EXTRACTION_TEMPLATE,
    format_keywords_for_prompt,
    format_services_batch,
    format_node_context_for_keywords,
)


class KeywordExtractor:
    """Extract functional keywords from services in sequential batches.

    Each batch receives the accumulated keyword list to avoid synonym duplication.
    For non-root nodes, keyword extraction is scoped to the node's functional domain.
    """

    def __init__(self, llm_client: LLMClient, config: AutoHierarchicalConfig):
        self.client = llm_client
        self.config = config

    def extract(
        self,
        services: List[Dict],
        node_info: Optional[dict] = None,
    ) -> Dict[str, int]:
        """Extract keywords from all services in batches.

        Args:
            services: Full service dicts with id, name, description
            node_info: Parent node info {name, description} for non-root nodes.
                       None for root node (no context added).

        Returns:
            {keyword: count} sorted by count descending
        """
        batch_size = self.config.keyword_batch_size
        n_batches = (len(services) + batch_size - 1) // batch_size
        keywords: Dict[str, int] = {}

        node_label = f" (within '{node_info['name']}')" if node_info else ""
        logger.info(f"Keyword Extraction: {len(services)} services{node_label}, {n_batches} batches")

        node_context_section = format_node_context_for_keywords(node_info)

        for i in range(0, len(services), batch_size):
            batch = services[i:i + batch_size]
            batch_num = i // batch_size + 1

            batch_keywords = self._extract_batch(batch, keywords, node_context_section)

            for kw, count in batch_keywords.items():
                keywords[kw] = keywords.get(kw, 0) + count

            progress_bar(batch_num, n_batches, suffix=f"{len(keywords)} keywords")

        # Sort by count descending
        keywords = dict(sorted(keywords.items(), key=lambda x: -x[1]))
        logger.info(f"Extraction complete: {len(keywords)} unique keywords")
        logger.info(f"Top 20: {list(keywords.keys())[:20]}")

        return keywords

    def _extract_batch(
        self,
        services: List[Dict],
        existing_keywords: Dict[str, int],
        node_context_section: str,
    ) -> Dict[str, int]:
        """Extract keywords from a single batch of services."""
        services_text = format_services_batch(services)
        existing_text = format_keywords_for_prompt(existing_keywords)

        prompt = KEYWORD_EXTRACTION_TEMPLATE.format(
            batch_size=len(services),
            max_keywords=self.config.max_keywords_per_service,
            existing_keywords_text=existing_text,
            services_text=services_text,
            node_context_section=node_context_section,
        )

        response = self.client.call(
            messages=[
                {"role": "system", "content": SYSTEM_KEYWORD_EXTRACTION},
                {"role": "user", "content": prompt},
            ],
            temperature=self.config.temperature_keywords,
            max_tokens=self.config.max_tokens_keywords,
        )

        if not response.success:
            logger.warning(f"Batch extraction failed: {response.error}")
            return {}

        result = parse_json_response(response.content)
        if not result or 'extractions' not in result:
            logger.warning(f"Failed to parse extraction response")
            return {}

        # Count keywords from this batch
        batch_keywords: Dict[str, int] = {}
        for extraction in result['extractions']:
            for kw in extraction.get('keywords', []):
                kw = kw.strip().lower().replace(' ', '_')
                if kw and len(kw) > 1:
                    batch_keywords[kw] = batch_keywords.get(kw, 0) + 1

        logger.info(f"Extracted {len(batch_keywords)} unique keywords from {len(services)} services")
        return batch_keywords
