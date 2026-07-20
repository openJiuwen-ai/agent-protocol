# A2X 增量构建模块设计文档（待实现）

**版本**: v0.1.0

本文档描述增量构建模块（`a2x_registry/a2x/incremental/`）的设计。系统整体视图见 [a2x_design.md](a2x_design.md)。

---

## 1. 流程逻辑说明

增量构建模块负责将增量新服务插入已有分类树，无需重新构建整棵树：

1. **递归定位**：LLM 从根节点开始，逐层选择最匹配的子分类，直到叶节点
2. **添加服务**：将新服务添加到最佳叶节点的服务列表中
3. **分支合并**：若目标节点的子分类数超过阈值（如 20），触发相似分支合并

## 2. 对外调用接口

```python
class IncrementalBuilder:
    def __init__(self, taxonomy: dict, class_data: dict, services_index: dict, llm_client: LLMClient, workers: int = 20): ...

    def add_service(self, service: Dict) -> List[str]:
        """
        将新服务增量插入分类树中。

        Args:
            service: 服务字典，需包含 id, name, description 字段

        Returns:
            分配到的分类 ID 列表

        Side Effects:
            修改内存中的分类树结构
        """

    def remove_service(self, service_id: str) -> bool:
        """从分类树中移除服务。"""

    def add_services_batch(self, services: List[dict]) -> Dict[str, List[str]]:
        """批量添加服务（并行）。"""
```

## 3. 逻辑视图

```mermaid
flowchart LR
    A([增量新服务]) --> B[递归定位]
    T1[(分类树)] --> B
    B --> C[添加服务]
    C --> D{子分类数>20?}
    D -->|否| T2[(更新后的分类树)]
    D -->|是| E[合并相似分支]
    E --> T2

    style A fill:#eceff1,stroke:#607d8b
    style T1 fill:#e8eaf6,stroke:#3f51b5,stroke-width:2px
    style T2 fill:#c8e6c9,stroke:#4caf50,stroke-width:2px
    style B fill:#bbdefb,stroke:#2196f3
    style C fill:#90caf9,stroke:#2196f3
    style D fill:#64b5f6,stroke:#2196f3
    style E fill:#42a5f5,stroke:#2196f3
```

## 4. 顺序图

```mermaid
sequenceDiagram
    participant U as 用户
    participant IB as 增量构建模块
    participant T as 分类树
    participant L as LLM

    U->>IB: add_service(新服务)
    IB->>T: 获取根节点

    rect rgb(227, 242, 253)
        Note over IB,L: 递归定位
        loop 从根到叶
            IB->>L: 服务描述 + 子分类列表
            L-->>IB: 最匹配的子分类
        end
        IB->>IB: 确定最佳叶节点
    end

    IB->>T: 添加服务到叶节点
    IB->>T: 检查子分类数

    alt 子分类数 > 20
        rect rgb(187, 222, 251)
            Note over IB,L: 分支合并
            IB->>L: 分析相似分支
            L-->>IB: 合并方案
            IB->>T: 执行合并
        end
    end

    IB-->>U: 添加成功
```

## 5. 类图

```mermaid
classDiagram
    class IncrementalBuilder {
        -taxonomy: Dict
        -class_data: Dict
        -services_index: Dict
        -llm: LLMClient
        -workers: int
        +add_service(service: Dict) List~str~
        +remove_service(service_id: str) bool
        +add_services_batch(services: List) Dict
        -_select_domains(service, domains) List~str~
        -_place_in_domains(service, domain_ids, domains) List~str~
        -_get_leaves_under(node_id: str) List~str~
    }

    class LLMClient {
        <<a2x_registry.common.llm_client>>
        +call(messages, temperature, max_tokens) LLMResponse
    }

    IncrementalBuilder --> LLMClient : uses
```
