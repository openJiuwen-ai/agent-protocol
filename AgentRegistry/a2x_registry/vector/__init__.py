"""向量检索模块

模块结构（与 a2x 一致）:
- build/: 索引构建 (IndexBuilder)
- search/: 向量检索 (VectorSearch)
- evaluation/: 评估框架 (VectorEvaluator, CLI: python -m a2x_registry.vector.evaluation)
- utils/: 通用组件 (EmbeddingModel, ChromaStore, metrics)
- data/: 向量数据库持久化目录
"""
