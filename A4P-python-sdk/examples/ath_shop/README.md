# A4P × ATH Shop

示例使用 A4P Python SDK 的 ATH 异步客户端，经官方 ATH Gateway 读取商城资料、查找耳机、加入购物车、查看购物车和最近订单，也可选择模拟下单。用户在浏览器完成商城 OAuth 授权后，返回演示页继续。SDK 通过 Gateway 交换对应会话的 ATH Token。

## 启动官方商城和 Gateway

参考 [ATH 官方 Gateway 模式部署文档](https://github.com/ath-protocol/demo/blob/1185068fe0e64983dc458053e0bd23a311764f97/demo/gateway/README.md)，启动商城和 Gateway 两个服务即可。按上游文档配置本地域名并让浏览器信任商城的开发证书。Gateway 应开放演示用户注册，发现文档应列出 `ath-shop` 和下表所需的权限。

在 macOS 上先打开 Docker Desktop 后运行 Compose：

```bash
# 在上游 ath-protocol/demo 的 demo/gateway 目录
docker compose up -d app gateway
curl -fsS http://gateway.ath.local:4001/.well-known/ath.json

# 将演示证书和 Agent 测试密钥导出到临时目录
ATH_SHOP_FILES="$(mktemp -d)"
umask 077
docker compose cp app:/app/certs/ca.crt "$ATH_SHOP_FILES/shop-ca.crt"
docker compose exec -T app cat /app/data/agent-private.pem > "$ATH_SHOP_FILES/agent-private.pem"
```

Agent URI 使用官方 Agent 身份服务在启动时公布的 Agent ID，默认是 `https://agent.ath.local:4000/.well-known/agent.json`。实际部署时应使用 Gateway 可访问的 Agent 身份服务与相匹配的密钥。若商城 OAuth 回调失败且 Gateway 启动时尚未加载商城 CA，在商城健康后重启 Gateway。


## 运行网页演示

在 `A4P-python-sdk` 目录启动本地服务，使用已导出的 Agent 密钥。

```bash
uv run --locked --extra ath python examples/ath_shop/web.py \
  --gateway http://gateway.ath.local:4001 --allow-http \
  --signup \
  --agent-id https://agent.ath.local:4000/.well-known/agent.json \
  --agent-key "$ATH_SHOP_FILES/agent-private.pem"
```

打开 [本地演示页面](http://127.0.0.1:8765)，填写网关用户名（默认 `a4p-user`，可修改）和密码后开始演示。打开商城授权页完成同意，再返回演示页交换令牌。核对整车商品和总价后可确认下单或跳过。随时点击运行记录顶部的「新建运行」，即可在当前操作收尾后清空记录并返回登录表单。已提交的商城操作仍然生效。服务一次运行一场演示，同一服务的标签页共享进度。

### 演示涉及的商城权限

| 权限 | 商城接口（相对 `/api`） | Demo 行为 |
| --- | --- | --- |
| `profile` | `GET /auth/me` | 显示商城用户名 |
| `products:read` | `GET /products` | 在种子目录页筛选有库存的商品 |
| `cart:write` | `POST /cart/add` | 加购两件 |
| `cart:read` | `GET /cart` | 查看加购后的商品、数量和整车总价 |
| `orders:read` | `GET /orders` | 显示最近 5 笔订单的编号、状态和总价 |
| `orders:write` | `POST /orders` | 确认后提交整车 |

演示每次运行都会申请 `orders:write`。点击「确认整车下单」后，商城使用固定测试地址创建模拟订单，清空购物车并更新库存。跳过下单时，加购的商品仍留在购物车。

当前 Gateway 代理会忽略查询字符串，所以示例在本地筛选商品。如果加购或下单结果不明确，先在商城核对购物车或订单历史。

## Agent 应用通过 A4P SDK 接入 ATH 服务

Agent 应用在工具调用层集成 A4P Python SDK 的 `a4p.integrations.ath` 适配层。适配层复用官方 ATH Python SDK，负责 Agent 注册、用户授权事务、令牌交换和业务请求。ATH Gateway 校验权限并将请求转发到目标 ATH 服务。

```text
Agent 应用 → A4P Python SDK（a4p.integrations.ath）→ ATH Gateway → ATH 服务
```

- 用 `ATHConfig` 配置一个 Gateway、Agent、provider 和固定的操作名、方法、路径及 scopes。发起授权时请求所配置操作的 scope 并集。用可信应用层完成 Gateway 登录，并把已认证的用户 ID 与登录 Token 封装为 `GatewaySession`。SDK 默认要求使用 HTTPS。
- 调用 `discover` 发现服务，首次发起授权时会自动检查或创建注册；需要单独查询审批结果时，可调用 `ensure_registered`。网关用户、Gateway 和 Agent 共同标识注册凭证。可注入 `RegistrationStore` 实现安全持久化，以便在重启后复用注册。
- 调用 `begin_authorization(application_session)` 获取授权链接和事务 ID。将该事务关联到可信应用会话及待执行参数。用户继续时调用 `resume_authorization(application_session, transaction_id)`。只把授权链接、事务 ID、状态和必要业务结果传给前端或模型。
- 只在结果为 `Authorized` 后按配置的操作名调用 `call_api`。用 `revoke` 撤销当前会话的 Token，用 `async with` 或 `aclose` 关闭客户端。关闭不会向 Gateway 发出撤销请求。
- 授权事务和 ATH Token 默认仅在当前进程内有效。Gateway 会话失效时，重新登录并用新登录凭证创建客户端。SDK 为此 Gateway 版本适配了 `X-ATH-User-Token`，令牌交换依据 Gateway 已完成的 OAuth 会话。应用无需收集或转交 IdP 授权码。

回归测试可用 `uv run --locked --extra ath pytest -q` 运行。
