"""通用文本嵌入服务"""

import os
from pathlib import Path
from typing import Dict, List, Union
import numpy as np
from sentence_transformers import SentenceTransformer

from a2x_registry.common.errors import VectorSearchUnavailableError

# ── Supported embedding models ────────────────────────────────────────────────

DEFAULT_EMBEDDING_MODEL = "all-MiniLM-L6-v2"

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


def _find_cached_model(model_name: str) -> str:
    """Return local snapshot path if model is cached, otherwise return model_name for remote download."""
    cache_dir = Path(os.environ.get("HF_HOME", Path.home() / ".cache" / "huggingface")) / "hub"

    # Try exact org/model format (e.g. shibing624/text2vec-base-chinese → models--shibing624--text2vec-base-chinese)
    sanitized = model_name.replace("/", "--")
    for prefix in (sanitized, f"sentence-transformers--{sanitized}"):
        model_dir = cache_dir / f"models--{prefix}" / "snapshots"
        if model_dir.exists():
            snapshots = [d for d in model_dir.iterdir() if d.is_dir()]
            if snapshots:
                return str(snapshots[0])
    return model_name  # Fall back to remote download


class EmbeddingModel:
    """文本嵌入模型封装

    使用 SentenceTransformer 将文本编码为向量表示。

    Args:
        model_name: 模型名称，默认 "all-MiniLM-L6-v2"

    Example:
        >>> model = EmbeddingModel()
        >>> embeddings = model.encode(["hello world", "goodbye"])
        >>> embeddings.shape
        (2, 384)
    """

    def __init__(self, model_name: str = DEFAULT_EMBEDDING_MODEL):
        self.model_name = model_name

        # Prefer local cache to avoid network; fall back to remote download if not cached
        local_path = _find_cached_model(model_name)
        cached_locally = local_path != model_name
        try:
            self.model = SentenceTransformer(local_path)
        except Exception as exc:
            hf_home = Path(
                os.environ.get("HF_HOME", Path.home() / ".cache" / "huggingface")
            )
            if cached_locally:
                # Cache existed but load still failed — corrupted or permissions
                hint = (
                    f"A cached copy was found at {local_path!r} but failed to load. "
                    f"The cache may be corrupted — delete {hf_home}/hub/ and retry "
                    f"on a connected host, or re-copy the cache from a working host."
                )
            else:
                hint = (
                    f"The model is not cached locally, so SentenceTransformer attempted "
                    f"to download it from HuggingFace Hub. This typically fails when:\n"
                    f"  1. HuggingFace is blocked by regional network restrictions.\n"
                    f"     Fix: set HF_ENDPOINT to a reachable mirror before the first "
                    f"call, e.g.\n"
                    f"         export HF_ENDPOINT=https://hf-mirror.com   # Linux/macOS\n"
                    f"         $env:HF_ENDPOINT='https://hf-mirror.com'   # PowerShell\n"
                    f"     then re-run the command. The model will be cached under\n"
                    f"         {hf_home}/hub/\n"
                    f"     so subsequent runs work offline.\n"
                    f"  2. This host has no internet. Download {model_name!r} on a\n"
                    f"     connected machine and copy the ~/.cache/huggingface/ folder "
                    f"over."
                )
            raise VectorSearchUnavailableError(
                f"Vector search is unavailable: could not load embedding model "
                f"{model_name!r}.\n\n{hint}\n\n"
                f"Original error: {type(exc).__name__}: {exc}"
            ) from exc

    def encode(self, texts: Union[str, List[str]], show_progress: bool = True) -> np.ndarray:
        """将文本编码为归一化向量

        Args:
            texts: 单个文本或文本列表
            show_progress: 是否显示进度条

        Returns:
            形状为 (n, dim) 的向量数组
        """
        return self.model.encode(
            texts,
            normalize_embeddings=True,
            show_progress_bar=show_progress
        )
