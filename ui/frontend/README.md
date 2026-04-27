# Frontend

A2X Registry 的网页 UI，基于 React + TypeScript + Vite + Tailwind + D3.js。

## 开发

```bash
npm install
npm run dev      # Vite 开发服务器，默认 http://localhost:5173
npm run build    # 构建生产版本到 dist/
npm run lint     # ESLint 检查
```

集成启动方式见仓根 `ui/launcher.py`：

```bash
python ui/launcher.py            # 自动判断 dev / prod 模式
python ui/launcher.py --no-frontend  # 仅起后端
```
