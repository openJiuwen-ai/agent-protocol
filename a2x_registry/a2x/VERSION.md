# A2X 版本说明

## 当前版本: v0.1.5

---

## v0.1.5 (2026-04-23)

### 概述
pip 打包正式发布版本。包名从 `src/` 重命名为 `a2x_registry/`，客户端 SDK 拆到独立仓库 [A2X-registry-client](https://github.com/Weizheng96/A2X-registry-client)；新增 6 个 CLI 入口；配置文件移至用户目录；Web UI 迁出 pip 包作为 clone-only demo。算法无变更。

### 新功能
- **pip 可装**：`pip install git+https://github.com/Weizheng96/A2X-registry.git@v0.1.5`
- **CLI 入口**：`a2x-backend` / `a2x-build` / `a2x-register` / `a2x-evaluate-a2x` / `a2x-evaluate-vector` / `a2x-evaluate-traditional`
- **客户端 SDK**：`a2x-registry-client` 独立包，`A2XRegistryClient` / `AsyncA2XRegistryClient` 双入口对称
- **路径抽象**：`A2X_REGISTRY_HOME` 环境变量 + `~/.a2x_registry/` 默认位置
- **友好错误**：`LLMNotConfiguredError` / `VectorSearchUnavailableError` 给出明确修复指引（含 HF 镜像建议）
- **Web UI 独立**：仓根 `ui/` 目录 + `python ui/launcher.py`，不进 pip 包

### 重构
- `src/` → `a2x_registry/`：59 处 import 全量迁移
- `llm_apikey.example.json` 进入 pip 包，`llm_apikey.json` 默认 `~/.a2x_registry/llm_apikey.json`
- 清理 `sys.path.insert` hack

### 评估结果
与 v0.1.4 一致（本次为打包重构，无算法变更）。

---

## v0.1.4 (2026-04-01)

### 概述
新增 Skill 注册（第三种服务类型），支持文件夹（ZIP）上传/下载/删除；服务查询新增 single 模式。代码重构：统一共享接口，消除跨模块冗余。

### 重构
- **统一 SearchResult**：三个搜索方法（a2x/vector/traditional）共用 `src.common.models.SearchResult`，消除 3 份重复定义
- **迁移 LLMClient**：从 `src.a2x.utils` 移至 `src.common.llm_client`，消除 traditional 对 a2x 的不合理依赖（原位置保留 re-export）
- **统一结果转换**：`SearchService.search()` 中三个分支的结果转换代码合并为一处
- **提取 compute_set_metrics**：三个 evaluator 的 precision/recall/hit 计算逻辑提取到 `src.common.evaluation`
- **扩充 common 模块**：新增 `models.py`、`llm_client.py`、`evaluation.py`，`common/__init__.py` 统一导出

### 新功能
- **Skill 注册**：`POST /api/datasets/{dataset}/skills` 上传 Skill ZIP，自动解析 `SKILL.md` frontmatter 注册为服务
- **Skill 下载/删除**：`GET /skills/{name}/download` 下载 ZIP、`DELETE /skills/{name}` 删除
- **Skill 自动发现**：启动时自动扫描 `database/{dataset}/skills/` 目录加载 skill 条目
- **单服务查询**：`GET /services?mode=single&service_id=xxx`，skill 类型返回 ZIP，其余返回 JSON
- **前端 AdminPanel**：注册 tab 新增 Skill 类型（文件夹选择 + JSZip 打包上传）；"列表"tab 改为"查询"（列表/查找双模式）；注销 tab 支持 skill 条目删除
- **三来源合并**：`user_config.json` + `api_config.json` + `skills/` 文件夹，优先级 api > user > skill

---

## v0.1.3 (2026-03-28)

### 概述
后端 API 重构、per-dataset Embedding 模型支持、数据集 CRUD、CN 数据集构建与评估、database 独立仓库。

### 新功能
- **数据集 CRUD**：`POST /api/datasets` 创建数据集、`DELETE /api/datasets/{dataset}` 删除数据集
- **Per-dataset Embedding 模型**：每个数据集可独立配置 Embedding 模型（en/zh/multilingual 三选一），通过 `vector_config.json` 持久化，模型切换自动触发向量索引全量重建
- **CN 数据集**：ToolRet_clean_CN、publicMCP_CN 中文翻译版，含独立分类树和评估结果
- **默认查询共享**：CN/EN 变体数据集共用同一套默认查询（`$ref` 重定向），切换时保持选中状态
- **ChromaDB 自动清理**：启动时自动删除不再使用的 ChromaDB collections
- **DatasetBrowser**：搜索界面"浏览数据集"面板，显示 Embedding 模型并支持切换
- **AdminPanel 多步预览**：新建数据集时预览显示两条请求（创建 + 注册首个服务）
- **AdminPanel 自动删除**：注销最后一条服务时自动删除空数据集

### 重构
- **后端 4 路由模块**：`register/router.py` 拆分为 `dataset.py`（CRUD/服务/分类树/Embedding）、`build.py`（构建/SSE）、`search.py`（搜索）、`provider.py`（LLM 切换）
- **API 路径统一**：`/api/registry/*` → `/api/datasets/*`，服务列表 3 端点合并为 `?mode=browse|admin|full`
- **移除冗余端点**：`/api/health`、`/api/registry/status`、`/api/registry/datasets`、`/api/registry/{dataset}/taxonomy-state`
- **SearchService**：Embedding 单例 → 按模型名缓存，`sync_vector` 检测模型变更自动重建
- **数据集重命名**：ToolRet_new → ToolRet_clean、ToolRet_CN → ToolRet_clean_CN
- **database 独立仓库**：数据文件迁移至 [A2X-registry-demo-data](https://github.com/Weizheng96/A2X-registry-demo-data)，父仓库仅保留 `.gitkeep`

---

## v0.1.2 (2026-03-25 ~ 2026-03-27)

### 概述
SSE 构建日志流、构建取消、服务注册模块上线、分页服务列表、CN 数据集。

### 新功能
- **SSE 构建日志流**：`GET /api/datasets/{dataset}/build/stream` 实时推送构建日志
- **构建取消**：`DELETE /api/datasets/{dataset}/build` 中断正在运行的构建
- **服务注册模块**：`src/register/` 支持 generic 和 A2A 两种服务类型注册/注销
- **分页服务列表**：支持 size/page 参数分页查询
- **AdminPanel**：管理员面板支持注册/注销/构建/浏览服务
- **CN 数据集**：ToolRet_CN、publicMCP_CN 中文翻译版

---

## v0.1.1 (2026-03-23 ~ 2026-03-24)

### 概述
搜索模块新增 get_one/get_important 模式 + stream 流式接口，新增交互式 UI 对比演示，构建模块增加断点续传，全面代码重构。

### 新功能
- **get_one 搜索模式**：`A2XSearch(mode="get_one")` 每步只选最相关的一个分类/服务，精度优先（功能精度 ~98%）
- **get_important 搜索模式**：`A2XSearch(mode="get_important")` 只选确定相关的分类/服务，平衡精度和召回
- **get_one fallback**：get_one 结果为空时自动 fallback 到 get_important 取第一个结果
- **stream 流式接口**：`search(query, stream=True)` 返回 Generator，实时 yield 导航步骤和最终结果
- **交互式 UI**：`python -m method.ui` 启动 FastAPI + React 前端，支持多方法并行对比、D3.js 分类树实时动画、LLM 相关性评判
- **构建断点续传**：`build(resume="yes")` 从上次中断处继续，`build(resume="keyword")` 复用关键词重构
- **评估命名标准化**：自动生成 `results/{date}_{method}-{mode}_{dataset}[-{suffix}]_{count}` 格式
- **评估 summary.json**：新增 `dataset`、`query_file`、`mode` 字段
- **Vector force_rebuild 修复**：`--force-rebuild` 参数现在正确传递到 IndexBuilder

### 重构
- **搜索模块拆分**（`a2x_search.py` → 5 个文件）：
  - `models.py`：数据类（SearchResult, SearchStats, NavigationStep 等）
  - `prompts.py`：prompt 模板 + parse_selection（纯函数）
  - `navigator.py`：CategoryNavigator（Phase 1 分类导航）
  - `selector.py`：ServiceSelector（Phase 2 服务筛选）
  - `a2x_search.py`：编排器（776→150 行）
  - SearchStats 内置线程锁，移除 stats_lock 参数
  - 移除冗余的 visited_ids/visited_lock（树结构无需去重）
  - 移除 search() 的 parallel/mode override（线程不安全）
  - get_one 模式下 _parse_selection 强制单选
- **构建模块**：
  - `NodeDesigner` → `CategoryDesigner`（含 design + refine 两职责）
  - NodeSplitter 内部创建 CategoryDesigner（TaxonomyBuilder 无需感知）
  - `split_node` 拆分为 `_classify_refine_loop` + `_finalize_assignments`
  - `build()` 接口：`resume="no"/"keyword"/"yes"` 替代 `resume` + `reuse_keywords` + `no_subdivision`
  - `build_status` 三阶段跟踪：`"bfs"` → `"cross_domain"` → `"complete"`
  - `output_dir` 从 `service_path` 自动推导，`service_path` 按数据集名比较
  - max_tokens 魔法数字提取为 config 常量
  - `refine_categories` 参数从 7 减至 6（移除 max_cats/max_tokens，加 is_root）
- **评估模块**（`a2x_evaluator.py`）：
  - `evaluate_batch` 拆分为 `_compute_overall_metrics` + `_print_results` + `_save_results`
- **公共模块**：新增 `method/common/naming.py`（评估命名工具，三方法共用）
- **清理**：删除空目录（build 旧子目录、method/intention）、废弃文档（docs/figure、docs/mermaid）

### 评估结果
与 v0.1.0 一致（重构不改变构建/搜索结果），新增 publicMCP 中文最新数据。

---

## v0.1.0 (2026-03-17)

### 概述
文档体系重构，发布正式版本。功能代码与 v0.0.10 一致，无算法变更。

### 变更
- **文档重构**：
  - README.md 重写（概述、评估结果、核心思想、快速开始、架构、适用场景、许可证）
  - 新增 `docs/a2x_design.md`（系统整体 + 搜索/注册/优化模块设计）
  - 新增 `docs/build_design.md`（构建模块设计）
  - 移除 `docs/logical_view.md`、`docs/sequence_diagram.md`（内容整合至设计文档）
- **版本标记**：所有文档统一为 v0.1.0
- **设计规范化**：每个模块包含流程逻辑、对外接口、逻辑视图、顺序图、类图
- **频率日志**：明确为独立外部输入，不再由搜索模块产生

### 评估结果
与 v0.0.10 一致，无变化。

---

## v0.0.10 (2026-03-11)

### 概述
Token 优化重构。用 generic_ratio 替代 confidence 分类规则，移除后处理模块（optimize/leaf_pusher/leaf_splitter），Root 服务从 241 降至 37（-85%），Token 消耗从 14,027 降至 7,051（-50%）。

**v0.0.9 vs v0.0.10 核心区别**：
- **v0.0.9**：基于 confidence 阈值分类，后处理管道（merge_small_leaves + push-to-leaves），Root 241 服务
- **v0.0.10**：基于 generic_ratio 分类（泛分类/正常/未分类三类），无后处理，Root 37 服务

### 核心变更
- **分类规则重构**（`node_splitter.py`）：
  - 移除 confidence 评分，改用 generic_ratio（默认 1/3）判断泛分类
  - 三类服务：正常（1 ≤ n_cats ≤ N*generic_ratio）、泛分类（>阈值→留父节点）、未分类（0→留父节点）
  - 迭代终止：无泛分类 + 无未分类 → 收敛
  - 超小子分类（≤ delete_threshold=2）迭代后删除
- **移除后处理模块**：
  - 删除 `optimize_taxonomy.py`、`leaf_pusher.py`、`leaf_splitter.py`
  - 删除 `--post-process` CLI 参数
- **分类验证简化**（`node_designer.py`）：
  - 移除程序化 forbidden pattern 检查，只保留 LLM 验证
- **构建输出**：
  - `auto_hierarchical/`：含 cross-domain
  - 构建时保留 `keywords.json` 供复用
- **新增 CLI 参数**：`--generic-ratio`（默认 0.333）、`--delete-threshold`（默认 2）

### 评估结果（ToolRet_new, 1839 services, 1714 queries）

#### 全量测试 (1714q)

| 指标 | v0.0.9 (1714q) | v0.0.10 (1714q) |
|------|:----:|:----:|
| Recall | **91.16%** | 88.03% |
| Precision | 14.40% | **17.47%** |
| Hit Rate | 94.11% | 91.25% |
| Avg LLM Calls | 7.78 | 7.94 |
| Avg Tokens/q | 14,027 | **7,051** |

**评估结果目录**：`results/20260311_a2x_toolretnew_all/`

### 分类结构 (ToolRet_new)
- 352 个分类节点（324 叶子），最大深度 3 层
- 4251 service assignments（含 487 跨域链接）
- Root 服务：37 个（从 v0.0.9 的 241 降 85%）
- 数据目录: `method/a2x/data/ToolRet_new/auto_hierarchical/`

### 运行方式
```bash
# 构建（默认含cross-domain，同时输出两版本）
python -m method.a2x.build \
  --service-path database/ToolRet_new/service.json \
  --output-dir method/a2x/data/ToolRet_new/auto_hierarchical

# 复用已有关键词
python -m method.a2x.build --reuse-keywords

# 可选参数
# --generic-ratio 0.333    泛分类阈值
# --delete-threshold 2     超小子分类删除阈值
# --no-cross-domain        禁用跨域分配
# --no-subdivision         仅根分类

# 评估
python -m method.a2x.evaluation \
  --data-dir method/a2x/data/ToolRet_new/auto_hierarchical \
  --max-queries 50 --workers 20 \
  --output results/{timestamp}_50
```

---

## v0.0.8 (2026-03-06)

### 概述
重构 `build/auto_hierarchical` 架构和分类逻辑。消除 Phase A/B 分离，统一为 BFS 递归流程。重新设计服务分类逻辑：LLM 自由列出所有相关分类、confidence 语义改为边界清晰度、三类问题服务驱动迭代精炼。

**v0.0.7 vs v0.0.8 核心区别**：
- **v0.0.7**：Phase A（根分类） + Phase B（调用 hierarchical 递归细分），分类逻辑有截断和静默清零
- **v0.0.8**：统一 BFS 递归（taxonomy_builder.py），分类逻辑更清晰（无截断、三类问题服务、容忍双归属）

### 核心变更
- **架构重构**：
  - 删除 `root_builder.py`、`category_designer.py`，新增 `node_splitter.py`、`taxonomy_builder.py`、`node_designer.py`
  - 统一 BFS 队列：root 和 child 使用相同代码路径（is_root 控制参数差异）
  - 不再需要单独调用 `build/hierarchical` 进行 Phase B 细分
- **分类逻辑重构**：
  - LLM 自由列出所有相关分类（移除 max_categories_per_service 截断）
  - 输入仅 service description（移除 ID 和 Name）
  - confidence 语义改为分类边界清晰度（而非归属确定性）
  - 三类问题服务：易混淆(2+类)、未分配(0类)、低置信度(1类+低conf)
  - 终止条件：n_problem + n_subcategories <= max_node_size
  - 迭代后容忍2类归属，3+类/未分配/低置信度留在当前节点
  - 精炼 prompt 使用问题服务驱动，分类每轮重新对所有服务分类
- **配置简化**：移除 max_categories_per_service、zero_low_confidence

### 评估结果（ToolRet_new, 1839 services, 1714 queries）

#### 全量测试 (1714q)

**实验目录**: `results/20260306_auto_hierarchical_v08_toolretnew_all/`

| 指标 | v0.0.7 (1714q) | v0.0.8 (1714q) | 变化 |
|------|:----:|:----:|:----:|
| Recall | **92.11%** | 89.41% | -2.70% |
| Hit Rate | **94.57%** | 92.36% | -2.21% |
| Precision | 10.66% | 8.49% | -2.17% |
| Avg LLM Calls | 20.53 | 18.13 | -2.40 |
| Avg Tokens/q | 15,117 | 15,832 | +715 |

#### 50q 采样

**实验目录**: `results/20260306_auto_hierarchical_v08_toolretnew_50/`

| 指标 | v0.0.7 (50q) | v0.0.8 (50q) | 变化 |
|------|:----:|:----:|:----:|
| Recall | 86.27% | 87.33% | +1.06% |
| Hit Rate | 94.00% | 92.00% | -2.00% |

### 分类结构 (ToolRet_new)
- 251个分类节点（209叶子），最大深度5层
- 3780 service assignments（含422跨域链接）
- 数据目录: `method/a2x/data/ToolRet_new/auto_hierarchical/`

### 运行方式
```bash
# 纯自动全流程（统一BFS递归：关键词→分类→分配→细分）
python -m method.a2x.build \
  --service-path database/ToolRet_new/service.json \
  --output-dir method/a2x/data/ToolRet_new/auto_hierarchical

# 仅根分类构建（跳过递归细分）
python -m method.a2x.build --no-subdivision
```

---

## v0.0.7 (2026-03-05)

### 概述
新增纯自动层次化分类构建模块 `build/auto_hierarchical`，全自动从服务中提取关键词→设计分类→分配服务→递归细分。全量 Recall 达到 **92.11%**（超越 v0.0.6 的 90.34%）。同时增加评估中间结果保存（JSONL增量检查点）和服务分配置信度数据保存。

**v0.0.6 vs v0.0.7 核心区别**：
- **v0.0.6**（半自动）：`build/hierarchical`，人工19分类 + 自动递归细分，Recall 90.34%
- **v0.0.7**（纯自动）：`build/auto_hierarchical`，关键词→分类→分配→细分全自动，Recall **92.11%**

### 核心变更
- **纯自动构建模块 `build/auto_hierarchical/`**：
  - Step 1 (`keyword_extractor.py`): 从1839服务中批量提取186个功能关键词（50/batch，累积去重）
  - Step 2 (`category_designer.py`): LLM将关键词频率聚合为15→19个顶层功能分类（经5轮迭代）
  - Steps 3-5 (`root_builder.py`): 逐服务分类→精炼→迭代收敛（复用hierarchical的prompt）
  - Phase B: 调用HierarchicalBuilder递归细分18个大节点（>40服务），生成330分类（279叶子），最大深度5层
- **服务分配置信度保存**：构建时保存 `assignments.json`，包含每个服务的分类、置信度和推理
- **评估中间结果保存**：评估过程中每条查询结果增量写入 `partial_results.jsonl`，支持断点续评

### 评估结果（ToolRet_new, 1839 services, 1714 queries）

#### 50q 采样

**实验目录**: `results/20260304_auto_hierarchical_full_toolretnew_50/`

| 指标 | v0.0.6 (hierarchical) | v0.0.7 (auto_hierarchical) | 变化 |
|------|:----:|:----:|:----:|
| Recall | 92.67% | 86.27% | -6.40% |
| Hit Rate | 96.00% | 94.00% | -2.00% |
| Precision | 11.07% | 8.25% | -2.82% |
| Avg LLM Calls | 11.76 | 22.84 | +11.08 |

> 注：50q采样存在较大偏差（见全量结果）

#### 全量测试 (1714q)

**实验目录**: `results/20260305_auto_hierarchical_full_toolretnew_all/`

| 指标 | v0.0.6 (1714q) | v0.0.7 (1714q) | 变化 |
|------|:----:|:----:|:----:|
| Recall | 90.34% | **92.11%** | **+1.77%** |
| Hit Rate | 92.94% | **94.57%** | **+1.63%** |
| Precision | 14.72% | 10.66% | -4.06% |
| F1 | - | 19.12% | - |
| Avg LLM Calls | - | 20.53 | - |
| Avg Tokens/q | - | 15,117 | - |

### 分类结构 (ToolRet_new)
- 330个分类节点（279叶子），最大深度5层
- 4155 service assignments（含604跨域链接）
- 数据目录: `method/a2x/data/ToolRet_new/auto_hierarchical_full/`

### 运行方式
```bash
# 纯自动全流程（关键词→分类→分配→细分）
python -m method.a2x.build \
  --service-path database/ToolRet_new/service.json \
  --output-dir method/a2x/data/ToolRet_new/auto_hierarchical

# 仅根分类构建（跳过细分）
python -m method.a2x.build --no-subdivision

# 对已有扁平分类运行细分
python -m method.a2x.build.hierarchical \
  --input-dir method/a2x/data/ToolRet_new/auto_hierarchical \
  --service-path database/ToolRet_new/service.json \
  --output-dir method/a2x/data/ToolRet_new/auto_hierarchical_full
```

---

## v0.0.6 (2026-03-04)

### 概述
在 v0.0.5 基础上引入4项改进，全量 Recall 突破 90% 目标（88.08% → **90.34%**）。核心改动包括缺失服务检测、零低置信度容忍、放宽多归属、跨域多归属。

**v0.0.5 vs v0.0.6 核心区别**：
- **v0.0.5**：156分类，132叶子，1824 service assignments，Recall 88.08%
- **v0.0.6**：196分类，166叶子，2899 service assignments（含574跨域链接），Recall **90.34%**

### 核心变更
- **缺失服务检测**：自动检测未被分配到任何分类的15个服务，放入root节点
- **零低置信度容忍**：迭代收敛要求 `n_low_conf == 0`，超限后 demote 到父节点
- **放宽同父多归属**：调整 prompt 鼓励合理双归属（"could reasonably be discovered"）
- **跨域多归属（新模块 `cross_domain_assigner.py`）**：
  - Phase 1：对每个有服务的叶子分类，LLM 判断哪些服务应同时出现在其他顶级域
  - Phase 2：将被选中的服务放入目标域的最佳叶子分类
  - 908个候选中559个服务获得跨域链接（574条）
- **单服务分类折叠**：后处理自动合并只有1个服务的叶子分类到父节点

### 评估结果（ToolRet_new, 1839 services, 1714 queries）

#### 50q 采样

**实验目录**: `results/20260303_hierarchical_refine_50/`

| 指标 | v0.0.5 | v0.0.6 | 变化 |
|------|:----:|:----:|:----:|
| Recall | 89.27% | **92.67%** | **+3.40%** |
| Hit Rate | 92.00% | **96.00%** | **+4.00%** |
| Precision | 9.24% | 11.07% | +1.83% |
| Avg LLM Calls | 11.90 | 11.76 | -0.14 |
| Avg Tokens/q | 9,349 | 9,162 | -187 |

#### 全量测试 (1714q)

**实验目录**: `results/20260304_hierarchical_refine_all/`

| 指标 | v0.0.5 (1714q) | v0.0.6 (1714q) | 变化 |
|------|:----:|:----:|:----:|
| Recall | 88.08% | **90.34%** | **+2.26%** |
| Hit Rate | 90.96% | **92.94%** | **+1.98%** |
| Precision | 15.32% | 14.72% | -0.60% |
| Error queries | 263 | **217** | **-46** |
| Type A (导航失败) | 188 | **158** | **-30** |

### 分类结构 (ToolRet_new)
- 196个分类节点（166叶子），最大深度5层
- 2899 service assignments（含574跨域链接）
- 数据目录: `method/a2x/data/ToolRet_new/hierarchical/`

### 运行方式
```bash
python -m method.a2x.build.hierarchical \
  --input-dir method/a2x/data/ToolRet_new/manual \
  --service-path database/ToolRet_new/service.json \
  --output-dir method/a2x/data/ToolRet_new/hierarchical
# 新增选项: --no-zero-low-confidence, --no-cross-domain
```

---

## v0.0.5 (2026-03-03)

### 概述
引入层次化分类构建器（`build/hierarchical`），在已有扁平分类基础上递归细分大分类节点，生成多层分类树。同步完成数据清洗（LLM 清洗 query.json 去除不合理标注），并在清洗后的均匀采样数据上进行多方案对比。

**v0.0.4 vs v0.0.5 核心区别**：
- **v0.0.4**（半自动/扁平）：`build/iterative --preserve-reference`，20个顶层扁平分类
- **v0.0.5**（半自动/层次化）：`build/hierarchical`，在扁平分类基础上递归细分大节点，156个分类（132叶子），最大深度4

### 核心变更
- **层次化构建器 `build/hierarchical`**：
  - 加载已有扁平分类（如 manual 输出），递归细分叶子节点（>40服务）
  - 每个节点：LLM设计子分类 → 逐服务分类(1/call, 20并发) → 精炼 → 迭代收敛
  - 24个节点被细分，生成156个分类（132叶子），最大深度4层
- **数据清洗脚本 `claude scripts/clean_query_with_llm.py`**：
  - LLM独立评估每条query-tool配对的合理性
  - 1878 → 1714 queries（14条不清晰，150条无合理工具）
  - 78条query的不合理工具标注被移除（共252个标注）
- **查询重排序**：按19个来源round-robin交错，前50条均匀覆盖所有来源
- **共享模块重构**：将 `build/iterative` 中的共享组件提取到 `build/common/`
- **删除旧模块**：移除 `build/iterative` 及其数据目录

### 评估结果（ToolRet_new, 1839 services, 1714 queries）

#### 6方法全面对比 (50q 均匀采样)

**实验目录**: `results/20260303_*_toolretnew_50/`

| 方案 | Recall | Hit Rate | Precision | Avg LLM Calls | Avg Tokens/q |
|------|:------:|:--------:|:---------:|:-------------:|:------------:|
| **hierarchical** (v0.0.5) | **89.27%** | 92.00% | 9.24% | 11.90 | 9,349 |
| manual (纯人工) | 87.20% | 94.00% | 16.48% | 2.98 | 9,566 |
| llm_taxonomy (纯自动) | 74.93% | 80.00% | 11.31% | 7.52 | 8,039 |
| traditional (MCP-style) | 66.67% | 68.00% | 4.38% | 1.00 | 66,730 |
| vector@10 | 66.67% | 74.00% | 9.60% | 0 | 0 |
| intention@10 | 55.07% | 66.00% | 7.60% | 1.00 | 280 |

#### 全量测试 (1714q)

| 方案 | Recall | Hit Rate |
|------|:------:|:--------:|
| **hierarchical** (1714q) | **88.08%** | **90.96%** |
| vector R@10 (1714q) | 69.06% | 75.73% |
| vector R@100 (1714q) | 90.02% | 92.77% |

### 分类结构 (ToolRet_new)
- 153个分类节点（129叶子），最大深度4层
- 基于 manual 输出扁平分类递归细分
- 数据目录: `method/a2x/data/ToolRet_new/hierarchical/`

### 运行方式
```bash
# 层次化构建（基于已有扁平分类）
python -m method.a2x.build.hierarchical \
  --input-dir method/a2x/data/ToolRet_new/manual \
  --service-path database/ToolRet_new/service.json \
  --output-dir method/a2x/data/ToolRet_new/hierarchical

# 评估（50q / 全量）
python -m method.a2x.evaluation.evaluation_a2x_toolretclean \
  --data-dir method/a2x/data/ToolRet_new/hierarchical \
  --query-file database/ToolRet_new/query.json \
  --service-path database/ToolRet_new/service.json \
  --max-queries 50 --workers 10 \
  --output results/{timestamp}_hierarchical_toolretnew_50
```

---

## v0.0.4 (2026-03-02)

### 概述
新增迭代构建器的先验分类保留模式（`--preserve-reference`），实现"人工分类质量 + 自动化服务分配"。在 ToolRet_clean 上全量评估 Recall 从 v0.0.3 纯自动的 77.23% 提升至 **82.98%**（1878q全量），50q 采样 Recall=84.33%。

**v0.0.3 vs v0.0.4 核心区别**：
- **v0.0.3**（纯自动）：`build/llm_taxonomy` 三阶段 pipeline，LLM 完全自主设计分类+分配服务
- **v0.0.4**（半自动）：`build/iterative --preserve-reference`，人工提供先验分类 + LLM 迭代分配服务并可扩展分类

> 注：v0.0.5 已将 `build/iterative` 的共享组件提取到 `build/common/`，并删除了 iterative 模块。v0.0.4 的分类数据可通过 git 历史查看。

### 评估结果（全量 1878 查询）

| 指标 | v0.0.3 纯自动 (1878q) | v0.0.4 半自动 (1878q) | 变化 |
|------|:--:|:--:|:--:|
| Recall | 77.23% | **82.98%** | **+5.75%** |
| Hit Rate | 81.74% | **87.91%** | **+6.17%** |
| Precision | 13.88% | **22.71%** | **+8.83%** |
| F1 | - | **35.66%** | - |
| Avg LLM Calls | 8.16 | **2.70** | **-66.9%** |

---

## v0.0.3 (2026-02-03)

### 概述
引入"清晰边界"分类设计原则，并优化搜索模块防止重复访问，Recall 提升至 81.33%，Token 消耗降低 75%。

### 核心变更
- **清晰边界分类设计**：
  - 每个分类增加 `boundary` 字段（负向定义：什么不属于此分类）
  - 每个分类增加 `decision_rule` 字段（明确的判断规则）
  - 类比医院分诊系统：每个服务明确知道去哪个分类
- **搜索优化 - 防重复访问**：
  - 添加 `visited_ids` 追踪已访问的分类节点
  - 线程安全的去重机制，避免多路径重复访问同一分类
- **新增分类架构图**：
  - `taxonomy_architecture.md`: 详细架构文档
  - `taxonomy_diagram.md`: Mermaid 可视化图表

### 评估结果
**实验目录**: `evaluation/results/20260203_171654_test_50/`

| 指标 | v0.0.2 | v0.0.3 | 变化 |
|------|------|------|------|
| Recall | 72.93% | **81.33%** | **+8.4%** |
| Hit Rate | 76.4% | **88.0%** | **+11.6%** |
| Precision | 11.57% | 11.43% | -0.1% |
| 平均LLM调用 | 53.48次 | **15.62次** | **-70.8%** |
| 平均Token消耗 | 95,248 | **23,380** | **-75.5%** |

### 分类结构
- 10 个顶层分类（不变）
- 61 个子分类（不变）
- 2000 个服务（不变）

### 设计原则
```
清晰边界 = 正向定义 + 负向定义 + 判断规则

例如：
- name: "Financial & Market Data"
- description: "Services for financial data, markets, investments"
- boundary: "Transaction processing goes to Transaction & Commerce"
- decision_rule: "If service provides market data, classify here"
```

---

## v0.0.2 (2026-02-03)

### 概述
采用 LLM Top-Down 分类构建方法，显著提升 Recall 并大幅降低搜索成本。

### 核心变更
- **新增 LLM Top-Down 分类构建流程**：
  1. Phase 1: LLM 定义 10 个顶层分类
  2. Phase 2: LLM 为每个顶层定义 3-6 个子分类（共 47 个）
  3. Phase 3: LLM 基于用户意图将服务分配到分类（支持多分类）
- **搜索默认使用 `llm_taxonomy/`** 目录的分类数据

### 评估结果

#### 500 查询完整测试
**实验目录**: `evaluation/results/20260203_101242_v0.2_full_500/`

| 指标 | v0.0.1 | v0.0.2 (500q) | 变化 |
|------|------|-------------|------|
| Recall | 62.8% | **72.93%** | +10.1% |
| Hit Rate | 66.2% | **76.4%** | +10.2% |
| Precision | 10.5% | **11.57%** | +1.1% |
| F1 | 15.9% | **19.97%** | +4.1% |
| 平均LLM调用 | 87.1次 | **16.05次** | -82% |
| 平均Token消耗 | 75,460 | **28,711** | -62% |

#### 100 查询初步测试
**实验目录**: `evaluation/results/20260202_llm_topdown_100/`

| 指标 | v0.0.1 | v0.0.2 (100q) | 变化 |
|------|------|-------------|------|
| Recall | 62.8% | 68.3% | +5.5% |
| Hit Rate | 66.2% | 74.0% | +7.8% |
| Precision | 10.5% | 8.7% | -1.8% |
| 平均LLM调用 | 87.1次 | 15.6次 | -82% |
| 平均Token消耗 | 75,460 | 27,540 | -64% |

### 目录结构
```
method/a2x/
├── build/
│   ├── llm_taxonomy/       # v0.0.2 分类构建 ★当前使用★
│   │   ├── phase1_framework.py
│   │   ├── phase2_subcategories.py
│   │   ├── phase3_assignment.py
│   │   └── convert_to_a2x_format.py
│   ├── clustering/         # v0.0.1 遗留（语义聚类）
│   ├── naming/             # 共享模块（llm_client.py）
│   └── correcting/         # v0.0.1 遗留（DAG校正）
├── search/                 # 递归搜索（默认使用llm_taxonomy）
├── evaluation/             # 评估脚本和结果
└── data/ToolRet_middle/
    ├── llm_taxonomy/       # v0.0.2 分类数据 ★当前使用★
    └── taxonomy/           # v0.0.1 分类数据（遗留）
```

### 运行评估
```bash
# 使用默认 llm_taxonomy
python -m method.a2x.evaluation.evaluation_a2x_toolretmiddle --max-queries 100

# 或使用专用评估脚本
python -m method.a2x.build.llm_taxonomy.evaluate_llm_taxonomy --max-queries 100
```

---

## v0.0.1 (2026-01-31)

### 概述
A2X 自动化分类构建与搜索的首个基线版本。

### 核心功能
- 三步分类构建流程：Clustering → Naming → Correcting
- LLM 递归搜索：基于分类树的多步推理服务发现

### 评估结果
**实验目录**: `evaluation/results/20260131_baseline_batch_500/`

| 指标 | 数值 |
|------|------|
| Recall | 0.628 (62.8%) |
| Precision | 0.105 (10.5%) |
| Hit Rate | 0.662 (66.2%) |
| 平均LLM调用 | 87.13次/查询 |
| 平均Token消耗 | 75,460 tokens/查询 |

### 已知问题
1. **分类归类错误 (28.6%)**: LLM 选错分支导致正确服务被剪枝
2. **服务未被选中 (6%)**: batch 模式在多工具查询中遗漏
3. **过度检索**: 平均返回 21.52 个服务，precision 仅 10.5%

---

## 版本历史

| 版本 | 日期 | 数据集 | Recall | 构建方式 | 主要变更 |
|------|------|--------|--------|---------|---------|
| v0.1.3 | 2026-03-28 | ToolRet_clean | 89.19% (1714q) | 纯自动 | 后端重构、per-dataset Embedding、数据集 CRUD、CN 评估 |
| v0.1.2 | 2026-03-27 | ToolRet_clean | 89.19% (1714q) | 纯自动 | SSE 构建流、构建取消、服务注册模块、AdminPanel |
| v0.1.1 | 2026-03-23 | ToolRet_clean | 89.19% (1714q) | 纯自动 | get_one/get_important 模式、stream 接口、交互式 UI |
| v0.1.0 | 2026-03-17 | ToolRet_clean | 88.03% (1714q) | 纯自动 | 文档体系重构，正式版本发布 |
| v0.0.10 | 2026-03-11 | ToolRet_clean | 88.03% (1714q) | 纯自动 | generic_ratio分类 + 移除后处理 + Token-50% |
| v0.0.9 | 2026-03-11 | ToolRet_clean | **91.16%** (1714q) | 纯自动 | 两阶段搜索 + 参数分离 + 小分类优化 |
| v0.0.8 | 2026-03-06 | ToolRet_clean | 89.41% (1714q) | 纯自动 | 统一BFS架构 + 分类逻辑重构 |
| v0.0.7 | 2026-03-05 | ToolRet_clean | **92.11%** (1714q) | 纯自动 | auto_hierarchical 关键词→分类→分配→细分 |
| v0.0.6 | 2026-03-04 | ToolRet_clean | 90.34% (1714q) | 半自动 | 跨域多归属 + 零低置信度 + 缺失服务检测 |
| v0.0.5 | 2026-03-03 | ToolRet_clean | 89.27% (50q) | 半自动 | hierarchical 层次化分类 + 6方法全面对比 |
| v0.0.4 | 2026-03-02 | ToolRet_clean | 82.98% (1878q) | 半自动 | iterative --preserve-reference |
| v0.0.3 | 2026-02-03 | ToolRet_middle | 81.33% (500q) | 纯自动 | 清晰边界分类 + 防重复访问优化 |
| v0.0.3 | 2026-02-28 | ToolRet_clean | 77.23% (1878q) | 纯自动 | llm_taxonomy 三阶段 pipeline (已删除，被auto_hierarchical取代) |
| v0.0.2 | 2026-02-03 | ToolRet_middle | 72.93% | 纯自动 | LLM Top-Down 分类构建 |
| v0.0.1 | 2026-01-31 | ToolRet_middle | 62.8% | 纯自动 | 首个基线版本 |

---

## 版本命名规范

- **v0.x**: 实验阶段版本
- **v0.0.10**: generic_ratio 分类 + Token 优化版本
- 每次迭代后更新此文档并 git commit

历史版本通过 git 管理：`git show <commit>:path/to/file`
