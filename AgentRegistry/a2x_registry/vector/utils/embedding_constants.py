"""Embedding model constants — lite-safe (zero heavy deps).

Lives inside the ``vector`` domain on purpose: these literals describe which
embedding models the vector backend can use. They're carved out into this
zero-dep submodule so non-vector callers (register/backend) can read them
without triggering numpy / sentence_transformers / chromadb imports.
"""

from typing import Dict

DEFAULT_EMBEDDING_MODEL: str = "all-MiniLM-L6-v2"

EMBEDDING_MODELS: Dict[str, dict] = {
    "all-MiniLM-L6-v2": {
        "dim": 384,
        "language": "en",
        "description": "English general-purpose (default)",
    },
    "shibing624/text2vec-base-chinese": {
        "dim": 768,
        "language": "zh",
        "description": "Chinese text embedding",
    },
    "paraphrase-multilingual-MiniLM-L12-v2": {
        "dim": 384,
        "language": "multilingual",
        "description": "Multilingual 50+ languages",
    },
}
