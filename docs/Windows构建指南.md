# CodePilot Windows 构建指南

## 前置要求

| 工具 | 用途 | 下载 |
|------|------|------|
| Visual Studio 2022 | C++ 编译 (MSVC) | https://visualstudio.microsoft.com |
| CMake ≥ 3.20 | 构建系统 | VS 自带或 `winget install Kitware.CMake` |

> 旧前端 `frontend/web/`（React Web）已弃用，当前客户端为 C++ CLI。

---

## 一、后端 + CLI 客户端（一体化 CMake 构建）

项目使用单一 CMakeLists.txt，一次构建产出两个 exe。

```powershell
# 1. 进入项目根目录
cd CodePilot

# 2. 生成 Visual Studio 工程
cmake -B build_win -G "Visual Studio 17 2022" -A x64

# 3. 编译
cmake --build build_win --config Release
```

### 产物位置

| 组件 | 路径 |
|------|------|
| 后端 Agent Server | `build_win\apps\agent-server\Release\codepilot-agent-server.exe` |
| CLI 客户端 | `build_win\apps\cli-client\Release\codepilot-cli.exe` |

---

## 二、启动

### 1. 先启动后端

```powershell
.\build_win\apps\agent-server\Release\codepilot-agent-server.exe
```

> 服务监听 `http://127.0.0.1:8080`。
> 首次使用需复制 `config/llm.local.example.json` 为 `config/llm.local.json` 并填写 API Key。

### 2. 再启动 CLI 客户端（另开终端）

```powershell
.\build_win\apps\cli-client\Release\codepilot-cli.exe
```

> CLI 支持命令：`task`（创建任务）、`list`（列出任务）、`status <id>`、`cancel <id>`、`delete <id>`、`tools`、`history <id>`、`active`、`help`

---

## 三、旧前端状态

`frontend/web/` 为 React + Vite + TypeScript 旧前端，代码结构完整，`npm install && npm run dev` 理论可启动，但已弃用，不推荐组员在此投入时间。

---

## 四、关键文件速查

| 用途 | 路径 |
|------|------|
| 后端入口 | `apps/agent-server/main.cpp` |
| CLI 客户端入口 | `apps/cli-client/main.cpp` |
| 路由注册 | `backend/agent-server/api/HttpServer.cpp` |
| Expert 配置 | `config/experts.json` |
| LLM 配置 | `config/llm.json` |
| API Key 配置 | `config/llm.local.json` |
| API 参照 | `docs/全API参照表.md` |
| 最终交付报告 | `docs/最终报告.md` |