# A2X 构建模块设计文档

**版本**: v0.1.1 (2026-03-23)

本文档详细描述构建模块（`a2x_registry/a2x/build/`）的设计。系统整体视图及其他模块设计见 [a2x_design.md](a2x_design.md)。

---

## 1. 流程逻辑说明

构建模块是分类树的生产者，负责将一份扁平的服务列表自动转化为层次化分类树（taxonomy.json + class.json），全程由 LLM 驱动，无需人工设计或维护目录结构。

整个构建过程以 **BFS 递归** 为主循环，从根节点开始，逐层拆分过大的节点。每个节点的拆分由 NodeSplitter 编排，包含以下步骤：

1. **分类设计**（CategoryDesigner.design）：根据节点规模自动选择策略——大节点（服务数 > keyword_threshold）先通过 KeywordExtractor 批量提取功能关键词，再基于关键词频率设计分类；小节点直接基于服务描述设计分类。仅 root 节点的关键词会缓存到 keywords.json 供复用，子节点的关键词不保存。Root 节点还会额外经过 LLM 验证和修正。
2. **服务分类**（NodeSplitter._classify_all）：将所有服务并行分配到子分类中（每服务 1 次 LLM 调用）。匹配过多子分类的服务标记为"泛分类"，无法匹配的标记为"未分类"，均留在父节点。
3. **收敛判断**（NodeSplitter._should_terminate）：若无泛分类且无未分类服务，或达到最大迭代次数，则收敛。
4. **迭代优化**（CategoryDesigner.refine）：未收敛时，将分类统计和问题服务反馈给 LLM，调整子分类方案后重新分类。
5. **边界处理**：删除过小的子分类（服务数 ≤ delete_threshold），将其服务归入未分类。

BFS 主循环（TaxonomyBuilder）在每个节点拆分后，将仍然过大的子节点入队继续细分，直至所有叶节点足够小或达到最大深度。每次拆分后立即保存中间结果，支持断点续传。

最后，**跨域多父归属**（CrossDomainAssigner）识别应同时出现在多个功能域的服务，将其链接到其他域的最佳叶分类，提升跨域可发现性。

## 2. 对外调用接口

### CLI 入口

```bash
# 完全重构（output-dir 默认为 database/{数据集名}/taxonomy）
python -m a2x_registry.a2x.build --service-path database/ToolRet_clean/service.json

# 断点续传（完成则跳过，中断则继续，配置变更则重建）
python -m a2x_registry.a2x.build --service-path database/ToolRet_clean/service.json --resume yes

# 复用关键词重构（调试用，跳过关键词提取）
python -m a2x_registry.a2x.build --service-path database/ToolRet_clean/service.json --resume keyword

# 可选参数
# --output-dir path          自定义输出目录（默认 database/{数据集名}/taxonomy）
# --generic-ratio 0.333      泛分类阈值
# --delete-threshold 2       超小子分类删除阈值
# --max-depth 3              最大递归深度（设为 1 可跳过递归细分）
# --no-cross-domain          禁用跨域分配
```

**数据集命名约定**：`service.json` 所在的上级目录名即为数据集名。例如 `database/ToolRet_clean/service.json` → 数据集名 `ToolRet_clean`，默认输出至 `database/ToolRet_clean/taxonomy`。

### Python 接口

```python
from a2x_registry.a2x.build import AutoHierarchicalConfig, TaxonomyBuilder

config = AutoHierarchicalConfig(
    service_path="your_services.json",
    # output_dir 默认为 "database/{数据集名}/taxonomy"
)

builder = TaxonomyBuilder(config)
builder.build()                    # 完全重构
builder.build(resume="yes")        # 断点续传
builder.build(resume="keyword")    # 复用关键词重构
```

### 配置项（AutoHierarchicalConfig）

```python
@dataclass
class AutoHierarchicalConfig:
    # 路径
    service_path: str = "database/ToolRet_clean/service.json"
    output_dir: str = None   # 默认 "database/{dataset_name}/taxonomy"

    # 关键词提取
    keyword_batch_size: int = 50       # 每批服务数
    max_keywords_per_service: int = 5  # 每个服务最多关键词数

    # 分类设计
    keyword_threshold: int = 500       # 大/小节点设计策略切换阈值
    max_categories_size: int = 20      # 每个节点最多子分类数

    # 分类/精炼
    generic_ratio: float = 1/3         # 泛分类阈值（匹配超过此比例的子分类则为泛分类）
    delete_threshold: int = 2          # 超小子分类删除阈值
    max_refine_iterations: int = 3     # 最大精炼迭代轮数

    # 树结构
    max_service_size: int = 40         # 叶节点最大服务数（超出则入队继续拆分）
    max_depth: int = 3                 # 最大递归深度

    # LLM max_tokens
    max_tokens_design: int = 6000       # 分类设计（关键词 / root 重设计）
    max_tokens_design_small: int = 4000 # 分类设计（描述 / 子节点精炼）
    max_tokens_classify: int = 300      # 单服务分类
    max_tokens_validate: int = 3000     # root 分类验证
    max_tokens_keywords: int = 4000     # 关键词提取（每批）

    # 并行
    workers: int = 20                  # 并行工作线程数

    # 跨域
    enable_cross_domain: bool = True   # 是否启用跨域多父归属
```

### 输入输出格式

**输入** — `service.json`：
```json
[
  {"id": "svc_1", "name": "FlightBooking", "description": "Book flights...", "inputSchema": {...}}
]
```

**输出** — `taxonomy.json`（树结构 + 构建状态）：
```json
{
  "version": "2.0-hierarchical",
  "root": "root",
  "build_status": "complete",
  "categories": {
    "root": {"children": ["cat_travel", "cat_finance"], "services": ["svc_generic"]},
    "cat_travel": {"children": ["cat_flights", "cat_hotels"], "services": []},
    "cat_flights": {"children": [], "services": ["svc_1", "svc_2"]}
  }
}
```

`build_status` 取值：`"bfs"`（BFS 拆分中）、`"cross_domain"`（BFS 完成，跨域待处理）、`"complete"`（构建完成）。

**输出** — `class.json`（分类元数据）：
```json
{
  "version": "2.0-hierarchical",
  "categories": {
    "cat_travel": {
      "name": "Travel & Tourism",
      "description": "Services for booking flights, hotels, and travel planning",
      "boundary": "Not for financial transactions or insurance",
      "decision_rule": "If the service helps users plan or book trips"
    }
  }
}
```

**输出** — `assignments.json`（分配详情，调试用）：
```json
{
  "svc_1": {"category_ids": ["cat_flights"], "reasoning": "Flight booking service"}
}
```

## 3. 逻辑视图

```mermaid
flowchart TB
    SVC([服务列表]) --> TB

    subgraph TB_BOX[TaxonomyBuilder — BFS 主循环]
        direction TB
        TB[从队列取出<br/>过大的节点] --> NS

        subgraph NS_BOX[NodeSplitter.split_node]
            direction TB
            NS[开始拆分] --> STRATEGY{服务数 ><br/>keyword_threshold?}
            STRATEGY -->|是| KE[关键词提取<br/>KeywordExtractor.extract]
            KE --> KW_DESIGN[基于关键词设计分类]
            STRATEGY -->|否| DESC_DESIGN[基于描述设计分类]
            KW_DESIGN --> ROOT_CHK{is_root?}
            ROOT_CHK -->|是| VALIDATE[验证 + 修正]
            ROOT_CHK -->|否| CLASSIFY
            VALIDATE --> CLASSIFY
            DESC_DESIGN --> CLASSIFY
            CLASSIFY[并行分类服务] --> CONVERGE{收敛?}
            CONVERGE -->|否| REFINE[迭代优化<br/>CategoryDesigner.refine]
            REFINE --> CLASSIFY
            CONVERGE -->|是| EDGE[边界处理<br/>泛分类/超小分类/未分类]
        end

        EDGE --> SAVE[中间保存]
        SAVE --> CHECK{子节点服务数<br/>> max_service_size?}
        CHECK -->|是| TB
        CHECK -->|否| DONE([所有叶节点<br/>足够小])
    end

    DONE --> CDA[跨域多父归属<br/>CrossDomainAssigner]
    CDA --> TAX[(taxonomy.json<br/>+ class.json)]

    style SVC fill:#eceff1,stroke:#607d8b
    style TAX fill:#e8eaf6,stroke:#3f51b5,stroke-width:2px
    style KE fill:#fff3e0,stroke:#ff9800
    style KW_DESIGN fill:#ffe0b2,stroke:#ff9800
    style DESC_DESIGN fill:#ffe0b2,stroke:#ff9800
    style REFINE fill:#fff3e0,stroke:#ff9800
    style CLASSIFY fill:#ffcc80,stroke:#ff9800
    style CDA fill:#fff9c4,stroke:#f9a825
    style STRATEGY fill:#ffab91,stroke:#ff5722
    style ROOT_CHK fill:#ffab91,stroke:#ff5722
    style CONVERGE fill:#ffab91,stroke:#ff5722
    style CHECK fill:#ffab91,stroke:#ff5722
    style DONE fill:#c8e6c9,stroke:#4caf50
```

## 4. 顺序图

```mermaid
sequenceDiagram
    participant U as 用户
    participant TB as TaxonomyBuilder
    participant NS as NodeSplitter
    participant CD as CategoryDesigner
    participant KE as KeywordExtractor
    participant CDA as CrossDomainAssigner
    participant L as LLM

    U->>TB: build(resume="no")
    TB->>TB: 加载服务, 初始化 taxonomy
    TB->>TB: build_status = "bfs"

    rect rgb(255, 243, 224)
        Note over TB,L: BFS 主循环 — 对每个过大的节点
        TB->>NS: split_node(node, services, is_root)

        rect rgb(255, 248, 225)
            Note over NS,L: Step 1: 分类设计 (CategoryDesigner.design)
            NS->>CD: design_categories(services, is_root)
            alt 大节点 (服务数 > keyword_threshold)
                CD->>KE: extract(services)
                loop 每批 50 服务
                    KE->>L: 服务描述 + 已有关键词
                    L-->>KE: keywords[]
                end
                KE-->>CD: 关键词频率表
                CD->>L: 基于关键词频率设计分类
                L-->>CD: categories[]
                opt 仅 root 节点
                    CD->>L: 验证 + 修正违规分类
                    L-->>CD: validated categories
                end
            else 小节点 (服务数 ≤ keyword_threshold)
                CD->>L: 基于服务描述设计分类
                L-->>CD: categories[]
            end
            CD-->>NS: 子分类方案
        end

        rect rgb(255, 224, 178)
            Note over NS,L: Step 2: 分类 + 精炼迭代
            loop 最多 max_refine_iterations 轮（默认 3）
                par 并行分类 (workers 线程)
                    NS->>L: 服务描述 + 子分类列表 → 选择所属分类
                    L-->>NS: category_ids
                end
                NS->>NS: 计算统计, 检查收敛
                break 收敛（无泛分类且无未分类）
                    NS->>NS: 退出迭代
                end
                NS->>CD: refine_categories(统计 + 问题服务)
                CD->>L: 调整子分类方案
                L-->>CD: refined subcategories
                CD-->>NS: 优化后的方案
            end
            NS->>NS: 边界处理（泛分类/超小分类/未分类）
        end

        NS-->>TB: NodeSplitResult
        TB->>TB: 应用结果, 中间保存
        TB->>TB: 过大的子节点入队
    end

    TB->>TB: build_status = "cross_domain"

    rect rgb(255, 249, 196)
        Note over TB,L: 跨域多父归属
        TB->>CDA: assign(taxonomy, class_data, services)
        loop 每个有服务的叶分类 (并行)
            CDA->>L: 服务 + 其他顶级域 → 识别跨域服务
            L-->>CDA: cross_assignments[]
        end
        loop 每个跨域服务 (并行)
            CDA->>L: 服务 + 目标域子分类 → 定位最佳叶分类
            L-->>CDA: target_leaf_id
        end
        CDA-->>TB: additions
    end

    TB->>TB: build_status = "complete", 保存
    TB-->>U: 构建完成
```

## 5. 类图

```mermaid
classDiagram
    class TaxonomyBuilder {
        -config: AutoHierarchicalConfig
        -taxonomy: Dict
        -class_data: Dict
        -services_index: Dict
        -all_assignments: Dict
        +build(resume: str) void
        -_evaluate_resume() str
        -_clean_output(preserve_keywords) void
        -_init_taxonomy() void
        -_load_existing_output() void
        -_load_services_index() void
        -_find_pending_nodes() Deque
        -_run_bfs(queue, splitter) int
        -_get_node_info(node_id) Dict
        -_apply_split_result(node_id, result) void
        -_accumulate_assignments(result) void
        -_apply_cross_domain(client) void
        -_save_output() void
        -_print_summary(elapsed, total_split) void
    }

    class NodeSplitter {
        -client: LLMClient
        -config: AutoHierarchicalConfig
        -designer: CategoryDesigner
        +split_node(node_id, parent_info, services, is_root) NodeSplitResult
        -_classify_refine_loop(node_id, parent_info, services, subcategories, is_root) tuple
        -_finalize_assignments(assignments, subcategories) List
        -_classify_all(services, subcategories, parent_info) Dict
        -_classify_single(service, ...) ClassificationResult
        -_compute_stats(assignments, subcategories) Dict
        -_should_terminate(stats, iteration, max_refine) bool
    }

    class CategoryDesigner {
        -client: LLMClient
        -config: AutoHierarchicalConfig
        -keyword_extractor: KeywordExtractor
        +design_categories(services, node_info, is_root, max_categories) Dict
        +refine_categories(parent_info, subcategories, assignments, services, stats, is_root) Dict
        -_design_from_keywords(services, ..., is_root) Dict
        -_design_from_descriptions(services, ...) Dict
        -_validate_and_fix_root_categories(categories, keywords) Dict
        -_load_cached_keywords() Dict
        -_save_keywords(keywords) void
    }

    class KeywordExtractor {
        -client: LLMClient
        -config: AutoHierarchicalConfig
        +extract(services, node_info) Dict~str, int~
        -_extract_batch(services, existing_keywords, ...) Dict
    }

    class CrossDomainAssigner {
        -client: LLMClient
        -workers: int
        +assign(taxonomy, class_data, services_index) Dict
        -_phase1_identify(...) List
        -_phase2_place(...) Dict
    }

    class AutoHierarchicalConfig {
        +service_path: str
        +output_dir: str
        +dataset_name: str [property]
        +keyword_threshold: int
        +max_service_size: int
        +max_depth: int
        +enable_cross_domain: bool
        +get_max_categories() int
        +build_params() Dict
        +matches_saved_config(path) bool
        +save(path) void
    }

    TaxonomyBuilder --> AutoHierarchicalConfig : uses
    TaxonomyBuilder --> NodeSplitter : creates
    TaxonomyBuilder --> CrossDomainAssigner : creates
    NodeSplitter --> CategoryDesigner : owns
    CategoryDesigner --> KeywordExtractor : owns
```
