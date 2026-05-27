# 意图授权（Intent Mandate）设计方案

## 1. 背景与定位

意图授权用于解决 Agent 在一段时间内重复执行同类工具调用时的授权问题。它不是给某一次精确调用签名，而是把用户同意的“可复用调用边界”表达成可验证、可过期、可约束的授权凭据。

在 A4P 中，意图授权与单次操作授权形成互补：

- `operation-mandate` 面向一次确定调用，例如 `delete_note(note_id="note-1")`。
- `intent-mandate` 面向一组可复用调用约束，例如允许 `delete_note(note_id=任意)`。
- `intent-token` 是用户批准 intent mandate 后，由服务端签发给 Agent 后续使用的短期凭据。

因此，意图授权的核心目标是：在不牺牲用户确认、参数边界和审计能力的前提下，降低重复授权带来的交互成本。

## 2. 设计思路

意图授权采用“先声明意图、再获得用户授权、最后签发短期 token”的三段式模型。

1. Agent 提出一个意图请求，描述允许调用哪些 `action`，以及每个 action 的 `params` 约束。
2. A4P 服务端生成 `intent-mandate`，用服务端签名固定该授权声明。
3. 用户授权器向用户展示可读授权内容，用户批准后追加用户签名。
4. 服务端验证服务端签名、用户签名、有效期和 server 归属。
5. 验证通过后，服务端签发 `intent-token`。
6. 后续敏感操作使用 token 校验 `action + params`。

这个模型把“用户批准了什么”和“Agent 后续能做什么”拆成两个层次：

- `intent-mandate` 记录用户批准的原始授权声明。
- `intent-token` 是服务端基于该声明签发的执行凭据。

## 3. 设计原则

### 3.1 工具调用优先

授权对象使用工具调用语义表达，而不是抽象资源模型。用户看到的 `displayText` 应清晰说明：

- 哪个 Agent 被授权；
- 可以调用哪些 action；
- 每个 action 允许使用哪些参数；
- 授权从何时开始、何时失效。

示例：

```text
授权 agent:demo-agent 在 ... 至 ... 期间调用 delete_note(note_id=任意)
```

### 3.2 最小权限

每个 intent mandate 必须显式包含 action 白名单和参数约束：

```json
{
  "intent": {
    "actions": [
      {
        "name": "delete_note",
        "params": {
          "note_id": "*"
        }
      }
    ]
  }
}
```

没有被写入 mandate/token 的 action 或参数，一律视为未授权。

### 3.3 参数边界明确

v1 参数约束只支持简单、可解释的规则：

- `params: "*"`：该 action 允许任意参数。
- `params: {}`：该 action 不允许携带参数。
- `params: {"key": "*"}`：实际调用必须包含 `key`，值任意。
- `params: {"key": "value"}`：实际调用必须精确等于该值。
- 实际调用出现未授权的额外参数时拒绝。

这种设计适合 MCP tool/function call 场景，避免协议层强行抽象跨 action 的资源模型。

### 3.4 可验证、不可篡改

授权声明和 token 都必须具备完整性保护。任何会影响授权语义的字段都必须进入 canonical JSON 后参与签名，包括：

- mandate 类型；
- mandate id；
- server；
- subject；
- intent actions；
- validTime；
- displayText；
- token subject/user/intent/expireAt/nonce。

校验方不得信任未签名字段。

### 3.5 短期有效、默认过期

意图授权默认有效期为 `3600` 秒。生产环境应根据动作风险降低有效期，并支持更严格策略，例如高风险动作仅允许分钟级授权。

### 3.6 双重确认边界

服务端签名表示“服务端生成了这份授权声明”，用户签名表示“用户批准了这份声明”。两者缺一不可。服务端不能只凭自己生成的 mandate 签发 token；用户授权器也不能自行扩大 mandate 内容。

### 3.7 可审计

每次授权和 token 使用都应能追溯到：

- `requestId`
- `mandateId`
- `tokenId`
- `subject.id`
- `user.id`
- `intent.actions`
- `approvedAt`
- `issuedAt`
- `expireAt`

## 4. 核心协议对象

### 4.1 IntentAuthorizationRequest

```json
{
  "agentId": "demo-agent",
  "userId": "user-1",
  "intent": {
    "actions": [
      {
        "name": "delete_note",
        "params": {
          "note_id": "*"
        }
      }
    ]
  },
  "validitySeconds": 3600,
  "metadata": {
    "reason": "cleanup old notes"
  }
}
```

### 4.2 IntentMandate

```json
{
  "type": "a4p/v1/intent-mandate",
  "mandateId": "mdt_xxx",
  "server": "local://a4p",
  "subject": {
    "type": "agent",
    "id": "agent:demo-agent"
  },
  "intent": {
    "actions": [
      {
        "name": "delete_note",
        "params": {
          "note_id": "*"
        }
      }
    ]
  },
  "validTime": {
    "start": "2026-05-20T02:00:00Z",
    "end": "2026-05-20T03:00:00Z"
  },
  "displayText": "授权 agent:demo-agent ...",
  "signatures": {
    "server": {
      "alg": "HS256",
      "keyId": "server#intent-mandate-k1",
      "signature": "..."
    },
    "user": {
      "alg": "HS256",
      "keyId": "user#intent-mandate-v1",
      "requestId": "a4p_intent_xxx",
      "approvalMethod": "local.web_ui",
      "approvedAt": "2026-05-20T02:01:00Z",
      "signature": "..."
    }
  }
}
```

### 4.3 IntentToken

token 复制 mandate 中的 action/params 约束，并加入 `tokenId`、`issuedAt`、`expireAt`、`nonce` 和服务端 token 签名。工具服务端校验 token 时提交实际调用：

```json
{
  "token": { "...": "..." },
  "expected": {
    "action": "delete_note",
    "params": {
      "note_id": "note-1"
    },
    "agentId": "agent:demo-agent",
    "userId": "user-1"
  }
}
```

## 5. 校验流程

`verify_intent_token()` 的校验顺序：

1. 校验 token 类型、签名、`keyId`、过期时间。
2. 校验 `subject.id` 和 `user.id`。
3. 在 token 的 `intent.actions` 中查找同名 action。
4. 校验实际 params 是否满足该 action 的 params 约束。
5. 返回 `valid/reason/code/matchedScope`。

## 6. 与 Operation Mandate 的关系

operation mandate 和 intent mandate 使用同一调用语义：

- operation mandate：精确一次 `action + params`。
- intent mandate：一组可复用 `action + params` 约束。

这让 MCP server、function calling、tool execution 等场景可以使用同一套授权语言。

## 7. 设计结论

新的 intent mandate 以 `action + params` 为核心抽象，更贴近 Agent 工具调用实际边界。它避免协议层强行统一业务资源模型，同时仍然保留最小权限、短期有效、双签名和可审计的安全属性。
