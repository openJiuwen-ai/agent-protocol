# IntelliRouter - 轻量LLM代理SDK

## 项目简介

**IntelliRouter** 是一个轻量级LLM代理SDK，实现多部署端点管理和智能路由。

核心特性:
- **API池化**: 单一模型名 → 多部署端点映射
- **策略插拔**: 支持SimpleShuffle、LowestLatency、TagBased、TokenAware、RateLimitAware、Adaptive等路由策略
- **状态管理**: 部署状态追踪、Token/RPM实一监控，失败计数，冷却机制
- **健庭检查**: 后台健庭检查循环
- **重试机制**: 自重试机制

## 代码结构设计

```
intelli_router/
├── __init__.py              公共接口
├── core/
│   ├── __init__.py
│   ├── deployment.py 部署配罫
│   ├── state.py      状态管理
│   ├── context.py 路由上下文
│   └── deployment.py 部署状态枚举
├── strategy/ 路由策略
│   ├── __init__.py 策略工厂
│   ├── base_strategy.py 抽象策略基类
│   ├── simple_shuffle.py 随机选择策略
│   ├── lowest_latency.py 最迪延迟策略
│   ├── tag_based.py 标签策略
│   ├── token_aware.py Token感知策略
│   ├── rate_limit_aware.py RPM限流策略
│   └──── adaptive.py 自适应策略
├── router/ 路由器
│   ├── __init__.py
│   ├── base_router.py 基础路一器
│   ├── reliable_router.py 可靠性路一器
├── cache/ 缓存
│   ├── __init__.py
│   └──── local_cache.py 本地缓存 (LRU)
├── health/ 健庭检查
│   ├── __init__.py
│   └──── checker.py SDK健庭检查
├── utils/ 工具函数
│   └──── exceptions.py 异常层次
├── tests/ 测试
      └── sdk_proxy_demo/
        ├── __init__.py
        ├── conftest.py pyest配置
        ├── test_api_pooling.py API池化测
        ├── test_routing_strategies.py 路由策略测
        ├── test_state_management.py 状态管理测
        ├── test_caching.py 缓存测
        ├── test_concurrent_requests.py 并发请测
        ├── test_adaptive_strategy.py 自适应策略测
        └── utils/
            ├── __init__.py
            ├── helpers.py 测试辅助函数
├── demo_agent_core.py Agent Core演示
├── demo_real_llm.py 真实LLM测试Demo
├── demo_adaptive_strategy.py 自适应策略演示
├── demo_real_llm_adaptive.py 真实LLM自适应策略Demo
```

## 路由策略介绍

本SDK实现了三种路由策略:

### 1. Token感知策略 (TokenAwareStrategy)

**场景**: Token耗尽时无缝切换

**策略**:
1. 过滤可用部署
2. 按Token剩余量排序
3. 优先择Token充足的部署
4. Token耗尽时无缝切换到下一个

**关键参数**:
- `token_threshold`: Token阈值 (默认1000)
- `exploration_ratio`: 探索比例 (默认0.1)

### 2. RPM限流策略 (RateLimitAwareStrategy)

**场景**: RPM限流时负载均衡

**策略**:
1. 过滤可用部署
2. 按RPM剩余配额排序
3. 优先择RPM配额充足的部署
4. RPM耗尽时无缝切换

**关键参数**:
- `rpm_threshold`: RPM阈值 (默认10)
- `exploration_ratio`: 探索比例 (默认0.1)

### 3. 自适应策略 (AdaptiveStrategy)

**场景**: 多级决策树

**策略**:
1. 健庭检查 → 咒除不健康部署
2. Token剩余 → 优先Token充足的
3. RPM剩余 → 优先RPM配额充足的
4. 延迟收益 → 优先低延迟的
5. 随机回退 → 随机选择

**评分公式**:
```
score = w_health * health_score
      + w_token * token_score
      + w_rpm * rpm_score
      + w_latency * latency_score
```

**关键参数**:
- `w_health`: 健庭权重 (默认1.0)
- `w_token`: Token权重 (默认0.5)
- `w_rpm`: RPM权重 (默认0.3)
- `w_latency`: 延迟权重 (默认0.2)

## 测试Demo使用方法

### 基础Demo (demo_adaptive_strategy.py)

**运行**:
```bash
python3 intelli_router/demo_adaptive_strategy.py
```

**输出**:
```
=== Token感知策略演示 ===
选择的部署: dep3
  - Token剩余: inf
  - Token使用率: 0.00%

=== 自适应策略演示 ===
部署评分:
  dep1: 得分=1.999 (健康, Token剩余5000, RPM剩余30, 延迟0.005s)
  dep2: 得分=2.000 (健康, Token剩余2000, RPM剩余50, 延迟0.002s) ← 最高
  dep3: 得分=0.800 (不健康)
选择的部署: dep2

=== Token耗尽无缝切换演示 ===
Token快耗尽时选择部署: dep1 (剩余100)
第1次请求后: Token剩余50
第2次请求后: Token耗尽, 切换到dep2
```

### 真实LLM Demo (demo_real_llm_adaptive.py)

**配置**:
```python
# 真实API Key (所有模型共用)
REAL_API_KEY = "sk-xxxx"

# 模型端口配置
MODEL_PORT_MAP = {
    "MiniMax-M2.7": [8001, 8002, 8003, 8004],
    "Qwen3.5-122B-A10B": [8005, 8006, 8007, 8008],
    "GLM-5": [8009, 8010, 8011, 8012],
    "GLM-5.1": [8013, 8014, 8015, 8016],
}
```

**运行**:
```bash
python3 intelli_router/demo_real_llm_adaptive.py
```

**输出**:
```
--- Task task-001: GLM-5.1 ---
  Selected deployment: GLM-5.1-port8013
  Latency: 10.84s

--- Task task-002: GLM-5.1 ---
  Selected deployment: GLM-5.1-port8013
  Latency: 11.29s

--- Task task-005: Qwen3.5-122B-A10B ---
  Selected deployment: Qwen3.5-122B-A10B-port8005
  Latency: 3.40s

[Agent Core] Dispatching 10 tasks concurrently...
[Agent Core] Completed 10 tasks in 13.42s
[Agent Core] Success rate: 10/10
```

### 测试文件 (test_adaptive_strategy.py)

**运行**:
```bash
python3 -m pytest intelli_router/tests/sdk_proxy_demo/test_adaptive_strategy.py -v
```

## 项目结构

详见 `sdk_llm` 目录树:
 
- `core/`: 核心和状态管理
- `strategy/`: 路由策略实现
- `router/`: 路由器实现
- `cache/`: 缓存实现
- `health/`: 健庭检查实现
- `tests/`: 测试用例
- `demo_*`: 演示脚本

