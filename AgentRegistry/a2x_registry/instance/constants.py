"""Instance 模块常量统一管理。

按约定集中存放本模块（a2x_registry.instance）的全部常量，并用注释区分：

- **可配置常量**：可经环境变量 / 装配参数调整（括号内标注环境变量名与默认值），
  由 ``backend/startup.py`` 读取后经构造函数注入，业务代码不直接读环境变量。
- **不可配置常量**：进程内固定值，调整需改代码。
"""

from __future__ import annotations

# ══ 可配置常量（环境变量注入）════════════════════════════════════════

# 元戎 frontend 地址；空 = 未配置（模式 B 请求 → 501 runtime_not_configured）
YUANRONG_ENDPOINT_ENV = "A2X_REGISTRY_YUANRONG_ENDPOINT"

# 元戎命名空间（registry 配置优先，不信任请求入参的 namespace）
# （A2X_REGISTRY_YUANRONG_NAMESPACE，默认 "default"）
YUANRONG_NAMESPACE_ENV = "A2X_REGISTRY_YUANRONG_NAMESPACE"
YUANRONG_NAMESPACE_DEFAULT = "default"

# 元戎单次 HTTP 超时秒数（A2X_REGISTRY_YUANRONG_TIMEOUT_SECONDS，默认 300）
YUANRONG_TIMEOUT_S_ENV = "A2X_REGISTRY_YUANRONG_TIMEOUT_SECONDS"
YUANRONG_TIMEOUT_S_DEFAULT = 300.0

# 模式 B 创建后等待元戎 status=running 的上限秒数；0 = 不等待立即返回
# （A2X_REGISTRY_YUANRONG_WAIT_RUNNING_SECONDS，默认 60）
YUANRONG_WAIT_RUNNING_S_ENV = "A2X_REGISTRY_YUANRONG_WAIT_RUNNING_SECONDS"
YUANRONG_WAIT_RUNNING_S_DEFAULT = 60.0

# 等待 running 的轮询间隔秒数（A2X_REGISTRY_YUANRONG_WAIT_INTERVAL_SECONDS，默认 1）
YUANRONG_WAIT_INTERVAL_S_ENV = "A2X_REGISTRY_YUANRONG_WAIT_INTERVAL_SECONDS"
YUANRONG_WAIT_INTERVAL_S_DEFAULT = 1.0

# ══ 不可配置常量（进程内固定）════════════════════════════════════════

# 同 service_id 在途操作互斥：锁获取等待上限（超时 → 409 in_progress）
LOCK_WAIT_S = 5.0

# 元戎成功后条目写入 / 删除的失败重试次数（仍失败 → 500 + 留日志待对账）
ENTRY_WRITE_RETRIES = 3

# DELETE ALL 批量调元戎的并发上限（asyncio.Semaphore）
ALL_CONCURRENCY = 8

# 九问 builtin 框架名：模式 B 里 framework == 该值 → kind = 九问
BUILTIN_AGENT_FRAMEWORK = "jiuwenswarm"

# 实例种类与生命周期状态（OpenAPI 枚举）
VALID_KINDS = ("三方", "九问")
VALID_STATUSES = ("运行", "停止", "异常")
STATUS_RUNNING = "运行"

# 元戎 GET /api/agent/:id 的实例状态语义
AGENT_RUNNING_STATUS = "running"
AGENT_FAILED_STATUSES = frozenset({"failed", "error", "deleted", "stopped", "killed"})
