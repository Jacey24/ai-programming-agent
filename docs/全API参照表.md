# CodePilot 全 API 参照表

> **版本**: v2.3  
> **更新**: 2026-07-13  
> **基础路径**: `http://{host}:8080/api/v1`

所有 API 返回统一 JSON 格式：
```json
{ "success": true, "data": { ... } }
// 或
{ "success": false, "error": { "code": "...", "message": "..." } }
```

---

## 一、系统 & 健康

| 方法 | 路径 | 说明 |
|------|------|------|
| `GET` | `/health` | 健康检查（兼容短路径） |
| `GET` | `/api/v1/health` | 健康检查 |

---

## 二、Session（会话）

| 方法 | 路径 | 说明 |
|------|------|------|
| `POST` | `/api/v1/sessions` | 创建新会话 |
| `GET` | `/api/v1/sessions` | 列出所有会话（`?page=&page_size=`） |
| `GET` | `/api/v1/sessions/:id` | 获取会话详情 |

**POST 请求体：**
```json
{ "title": "会话标题" }
```

---

## 三、Workspace（工作区）

| 方法 | 路径 | 说明 |
|------|------|------|
| `POST` | `/api/v1/workspaces` | 创建工作区 |
| `GET` | `/api/v1/workspaces` | 列出所有工作区 |
| `GET` | `/api/v1/workspaces/:id` | 获取工作区详情 |
| `GET` | `/api/v1/workspaces/:id/files/tree` | 工作区文件树 |
| `GET` | `/api/v1/workspaces/:id/files/content` | 文件内容 |

**POST 请求体：**
```json
{ "name": "工作区名称", "path": "/workspace" }
```

---

## 四、Task & 对话（核心 AI 任务）

> **v2.3 变更**：Task 归属从 `session_id` 改为 `global_id`。跨任务上下文通过 `global_context` 表自动检索，不再使用 `parent_task_id` 串联。

| 方法 | 路径 | 说明 |
|------|------|------|
| `POST` | `/api/v1/tasks` | 创建并异步执行任务 |
| `POST` | `/api/v1/tasks/continue` | **兼容端点（行为同 createTask）** — v2.3 已简化为普通创建 |
| `GET` | `/api/v1/tasks` | 列出任务（`?page=&page_size=`） |
| `GET` | `/api/v1/tasks/:id` | 任务详情 |
| `GET` | `/api/v1/tasks/active` | 活跃任务内存快照 |
| `POST` | `/api/v1/tasks/:id/cancel` | 取消任务 |
| `DELETE` | `/api/v1/tasks/:id` | 删除任务（非 running）+ 级联清理 |
| `GET` | `/api/v1/tasks/:id/tool-calls` | 工具调用记录 |
| `GET` | `/api/v1/tasks/:id/logs` | 任务日志 |
| `GET` | `/api/v1/tasks/:id/file-changes` | 文件变更记录 |
| `GET` | `/api/v1/tasks/:id/replay` | 任务回放 |
| `GET` | `/api/v1/tasks/:id/events/history` | 事件历史 |
| **`GET`** | **`/api/v1/tasks/:id/events`** | **SSE 实时事件流（长连接）** |

**POST `/api/v1/tasks` 创建任务请求体：**
```json
{
  "global_id": "g_default",
  "workspace_id": "w_xxx",
  "input": "帮我创建一个排序算法"
}
```

> **兼容性**：`session_id` 作为 `global_id` 的别名仍可接受。若都不传，自动使用默认 Global `g_default`。

**POST `/api/v1/tasks/continue` 兼容端点请求体：**
```json
{
  "global_id": "g_default",
  "workspace_id": "w_xxx",
  "input": "继续完善之前的排序算法，加上性能测试"
}
```

> **v2.3 行为**：此端点内部重定向到 `createTask()`，不再解析 `parent_task_id`。跨任务上下文由 AgentLoop 自动从 `global_context` 表检索最近的 summary 注入首轮 prompt。

**创建任务响应：**
```json
{
  "success": true,
  "data": {
    "id": "t_xxx",
    "global_id": "g_default",
    "workspace_id": "w_xxx",
    "goal": "帮我创建一个排序算法",
    "status": "running",
    "created_at": "2026-07-13T...",
    "updated_at": "2026-07-13T..."
  }
}
```

### 活跃任务快照

**`GET /api/v1/tasks/active`** 响应：
```json
{
  "success": true,
  "data": {
    "items": [
      {
        "task_id": "t_xxx",
        "global_id": "g_default",
        "workspace_id": "w_xxx",
        "goal": "创建排序算法",
        "current_expert": "executor",
        "current_stage": "expert_start",
        "expert_chain": ["planner", "executor"],
        "status": "running"
      }
    ]
  }
}
```

### 删除任务

**`DELETE /api/v1/tasks/:id`** 约束：
- 运行中（`status == "running"`）的任务不可删除，返回 **409 Conflict**
- 已完成/已失败/已取消的任务级联删除 events、logs、tool_calls

**成功响应：**
```json
{
  "success": true,
  "data": {
    "id": "t_xxx",
    "deleted": true
  }
}
```

**失败响应（运行中任务）：**
```json
{
  "success": false,
  "error": {
    "code": "TASK_RUNNING",
    "message": "cannot delete a running task"
  }
}
```

### SSE 事件类型

| event: 行 | channel | 说明 |
|-----------|---------|------|
| `task_created` | status | 任务已创建 |
| `agent_message` | dialog/status/debug | 通用消息 |
| `agent_message_chunk` | dialog | 流式消息片段 |
| `tool_started` | debug | 工具开始执行 |
| `tool_output` | debug | 工具执行输出 |
| `tool_finished` | debug | 工具执行完成 |
| `permission_required` | status | 需要用户授权 |
| `permission_resolved` | status | 权限已裁决 |
| `file_changed` | status | 文件已变更 |
| `task_completed` | status | 任务完成 |
| `task_failed` | status | 任务失败 |
| `task_cancelled` | status | 任务已取消 |
| `stream_end` | — | SSE 流结束 |

SSE 数据六字段：`id` `task_id` `type` `content` `metadata` `created_at`。

详细协议见 `docs/前端事件协议规范文档.md`。

---

## 五、Tools（工具注册表）

| 方法 | 路径 | 说明 |
|------|------|------|
| `GET` | `/api/v1/tools` | 列出所有工具（`?group=file`） |
| `GET` | `/api/v1/tools/:name` | 获取工具详情 |

---

## 六、Permissions（权限）

| 方法 | 路径 | 说明 |
|------|------|------|
| `GET` | `/api/v1/permissions/pending` | 列出待处理权限 |
| `GET` | `/api/v1/permissions/:id` | 获取权限详情 |
| `POST` | `/api/v1/permissions/:id/approve` | 批准权限 |
| `POST` | `/api/v1/permissions/:id/reject` | 拒绝权限 |

---

## 七、Experts（Expert Chain 可视化 & CRUD）🆕

### 7.1 图结构（流程图数据源）

| 方法 | 路径 | 说明 |
|------|------|------|
| `GET` | `/api/v1/experts/graph` | 获取完整图结构（nodes + edges + virtual_nodes） |
| `GET` | `/api/v1/experts/graph/positions` | 获取画布坐标 |
| `PUT` | `/api/v1/experts/graph/positions` | 保存画布坐标 |

**graph 响应：** 详见 §十 图结构协议。

**positions 请求体：**
```json
{ "planner": {"x": 100, "y": 200}, "executor": {"x": 400, "y": 200} }
```

### 7.2 Expert 列表 & 详情

| 方法 | 路径 | 说明 |
|------|------|------|
| `GET` | `/api/v1/experts` | 列出所有 Expert（摘要） |
| `GET` | `/api/v1/experts/:name` | 获取单个 Expert 完整配置 |

### 7.3 Expert CRUD

| 方法 | 路径 | 说明 |
|------|------|------|
| `POST` | `/api/v1/experts` | 创建新 Expert |
| `PUT` | `/api/v1/experts/:name` | 全量更新 Expert |
| `PATCH` | `/api/v1/experts/:name` | 局部更新 Expert（JSON merge） |
| `DELETE` | `/api/v1/experts/:name` | 删除 Expert |

**POST/PUT 请求体示例：**
```json
{
  "name": "code_reviewer",
  "description": "你是代码审查专家...",
  "is_entry": false,
  "context_isolation": true,
  "visible_tools": ["file.read", "git.diff"],
  "can_modify_plan": false,
  "can_write_summary": true,
  "read_global_actively": false,
  "llm_provider": "",
  "llm_model": "",
  "llm_timeout": 60,
  "llm_temperature": 0.5,
  "max_internal_rounds": 5,
  "tool_timeout_seconds": 60,
  "next_rules": [
    {"type": "tag_exists", "value": "done", "route_to": "summarizer", "priority": 10}
  ],
  "on_fail": "summarizer"
}
```

### 7.4 Expert 子资源（工具 & 路由 & LLM）

| 方法 | 路径 | 说明 |
|------|------|------|
| `PUT` | `/api/v1/experts/:name/tools` | 设置可见工具列表（全量替换） |
| `POST` | `/api/v1/experts/:name/tools` | 追加一个工具 `{"tool_name": "shell.run"}` |
| `DELETE` | `/api/v1/experts/:name/tools/:tool` | 移除一个工具 |
| `PUT` | `/api/v1/experts/:name/routes` | 设置路由规则（全量替换） |
| `POST` | `/api/v1/experts/:name/routes` | 追加一条路由规则 |
| `DELETE` | `/api/v1/experts/:name/routes/:index` | 删除路由规则（按索引） |
| `PUT` | `/api/v1/experts/:name/llm` | 更新 Expert LLM 配置 `{"provider":"deepseek","model":"xxx","timeout":120,"temperature":0.3}` |

### 7.5 导入导出 & 验证 & 预览

| 方法 | 路径 | 说明 |
|------|------|------|
| `GET` | `/api/v1/experts/export` | 导出完整 experts.json |
| `POST` | `/api/v1/experts/import` | 导入完整配置 |
| `POST` | `/api/v1/experts/validate` | 验证配置有效性（不实际应用） |
| `POST` | `/api/v1/experts/:name/prompt/preview` | 预览构建后的 prompt |

**preview 请求体：**
```json
{ "goal": "创建一个排序算法", "plan": "分析需求", "summary": "" }
```

### 7.6 全局 LLM 默认值

| 方法 | 路径 | 说明 |
|------|------|------|
| `GET` | `/api/v1/experts/llm/defaults` | 获取全局 LLM 默认配置 |
| `PUT` | `/api/v1/experts/llm/defaults` | 设置全局 LLM 默认配置 |

---

## 八、Config（全局配置文件）🆕

### 8.1 全局配置合并视图

| 方法 | 路径 | 说明 |
|------|------|------|
| `GET` | `/api/v1/config` | 获取所有配置文件合并视图 |

### 8.2 agent.json

| 方法 | 路径 | 说明 |
|------|------|------|
| `GET` | `/api/v1/config/agent` | 获取 agent.json |
| `PUT` | `/api/v1/config/agent` | 更新 agent.json |

### 8.3 llm.json

| 方法 | 路径 | 说明 |
|------|------|------|
| `GET` | `/api/v1/config/llm` | 获取 llm.json |
| `PUT` | `/api/v1/config/llm` | 更新 llm.json |
| `POST` | `/api/v1/config/llm/test` | LLM 连接测试 `{"prompt": "Hello"}` |

### 8.4 LLM Provider 管理

| 方法 | 路径 | 说明 |
|------|------|------|
| `GET` | `/api/v1/config/llm/providers` | 列出所有 Provider |
| `POST` | `/api/v1/config/llm/providers` | 添加 Provider `{"id":"xxx","base_url":"...","model":"...","api_key_env":"..."}` |
| `PUT` | `/api/v1/config/llm/providers/:id` | 更新 Provider |
| `DELETE` | `/api/v1/config/llm/providers/:id` | 删除 Provider |

### 8.5 workspace.json

| 方法 | 路径 | 说明 |
|------|------|------|
| `GET` | `/api/v1/config/workspace` | 获取 workspace.json |
| `PUT` | `/api/v1/config/workspace` | 更新 workspace.json |

### 8.6 logging.json

| 方法 | 路径 | 说明 |
|------|------|------|
| `GET` | `/api/v1/config/logging` | 获取 logging.json |
| `PUT` | `/api/v1/config/logging` | 更新 logging.json |

### 8.7 tools.json 🆕

| 方法 | 路径 | 说明 |
|------|------|------|
| `GET` | `/api/v1/config/tools` | 获取 tools.json（完整工具注册表） |
| `PUT` | `/api/v1/config/tools` | 更新 tools.json（全量替换） |

**PUT 请求体** 与 `config/tools.json` 格式一致，每个工具定义包含 `name`、`description`、`risk_level`、`enabled`、`params`、`category`。

### 8.8 llm.local.json 🆕

| 方法 | 路径 | 说明 |
|------|------|------|
| `GET` | `/api/v1/config/llm/local` | 获取 llm.local.json（**api_key 脱敏显示**） |
| `PUT` | `/api/v1/config/llm/local` | 更新 api_key（**热加载**到 LlmClientFacade） |

**GET 响应——脱敏后的 key：**
```json
{
  "success": true,
  "data": {
    "api_key": "sk-9****17a",
    "api_key_masked": true
  }
}
```

**PUT 请求体——更新全局 api_key：**
```json
{
  "api_key": "sk-new-api-key"
}
```

**PUT 请求体——更新特定 provider 的 api_key：**
```json
{
  "providers": {
    "deepseek": { "api_key": "sk-deepseek-key" },
    "openai": { "api_key": "sk-openai-key" }
  }
}
```

> PUT 后自动调用 `LlmClientFacade::reloadConfig()`，新 key 立即生效，无需重启服务。

---

## 九、后端核心模块暴露的功能接口（供 Orchestrator 使用）

| 模块 | 单例 | 核心方法 |
|------|------|---------|
| **AgentConfiguration** | ✅ | `init/reconfigure/exportJson/importJson/saveToFile/getExpert/getEntryExpert/listExpertNames/allExperts/addExpert/updateExpert/patchExpert/removeExpert/setExpertTools/setExpertRoutes/setExpertLlm/getGlobalLlmDefaults/setGlobalLlmDefaults/validate` |
| **AgentLoop** | ❌ (每 task 创建) | `run(taskId, globalId, workspaceId, goal, cancelFlag)` → `AgentLoopResult` |
| **AgentOrchestrator** | ✅ | `init/startTask/cancelTask/registerFrontend/unregisterFrontend/activeTasks/getTaskState/isReady/finalizeTask` |
| **PromptBuilder** | 静态工具类 | `buildInitial(expert, ctx)` / `buildNextRound(expert, ctx, sessionHistory, lastOutput, roundsLeft)` |
| **TaskContext** | 值对象 | `taskId/globalId/goal/currentPlan(summary)/planHistory/currentStage` |
| **EventBus** | 通过 ToolSystem 获取 | `subscribeByTaskId/publish/unsubscribe` |
| **SSEGateway** | ✅ | `push/pushDialog/pushStatus/pushDebug/pushStream/streamTaskEvents` |
| **ToolSystem** | ✅ | `callToolWithPermission/listToolNames/getToolSchemas/eventBus` |
| **LlmClientFacade** | ✅ | `chat/init/isAvailable` |
| **DataAccessFacade** | ✅ | `createTask/getTask/updateTaskStatus/listTasksBySession/listRecentTasks/saveEvent/appendLog/getLogsByTaskId/saveToolCall` |

---

## 十、图结构协议（`GET /api/v1/experts/graph` 响应）

```json
{
  "success": true,
  "data": {
    "nodes": [
      {
        "id": "planner",
        "label": "planner",
        "description": "你是任务规划专家...",
        "is_entry": true,
        "is_exit": false,
        "context_isolation": false,
        "visible_tools": [],
        "permissions": {
          "can_modify_plan": true,
          "can_write_summary": true,
          "read_global_actively": false
        },
        "llm": { "provider": "", "model": "", "temperature": 0.3, "timeout": 0 },
        "limits": { "max_internal_rounds": 3, "tool_timeout_seconds": 60 },
        "on_fail": "_done",
        "position": { "x": 100, "y": 200 }
      }
    ],
    "edges": [
      {
        "id": "edge_1",
        "source": "planner",
        "target": "executor",
        "condition_type": "tag_exists",
        "condition_value": "plan",
        "priority": 10,
        "label": "输出 <plan>"
      }
    ],
    "virtual_nodes": [
      { "id": "_done", "label": "出口", "type": "exit" },
      { "id": "_user", "label": "入口", "type": "entry" }
    ]
  }
}
```

---

## 十一、补全后端接口的标准链路指引 🆕

> 当需要新增一条 HTTP API 时，按以下 **5 层自上而下** 的修改路径逐一补全，可确保不遗漏任何环节。

### 标准链路（以 `POST /api/v1/tasks/continue` 为例）

```
┌─ 1. 数据层（底层）─────────────────────────────────────────────────┐
│ TaskContext.h                                                      │
│   → 新增字段：parentTaskId, loadedHistory, loadedPlanJson,        │
│     loadedSummary, sessionId                                      │
│ PromptBuilder.h / .cpp                                             │
│   → 新增静态方法：buildContinue(expert, ctx)                       │
│                                                                   │
├─ 2. 领域层（核心逻辑）─────────────────────────────────────────────┤
│ AgentLoop.h / .cpp                                                 │
│   → 新增方法：runContinue(taskId, ..., parentTaskId)              │
│   → 新增静态方法：loadHistoryFromParent(parentTaskId, ...)        │
│                                                                   │
├─ 3. 编排层（单例协调）─────────────────────────────────────────────┤
│ AgentOrchestrator.h / .cpp                                         │
│   → 新增方法：continueTask(sessionId, workspaceId, goal,          │
│                            parentTaskId)                          │
│   → 新增内部方法：runContinueThread(...)                           │
│                                                                   │
├─ 4. Controller 层（HTTP 处理）─────────────────────────────────────┤
│ TaskController.h / .cpp                                            │
│   → 新增方法：continueTask(request)                               │
│   → 添加 #include "domain/agent/AgentOrchestrator.h"              │
│                                                                   │
├─ 5. 路由层（HTTP Server 注册）─────────────────────────────────────┤
│ HttpServer.cpp                                                     │
│   → 在 handleRequest() 中添加：                                   │
│     if (request.rfind("POST /api/v1/tasks/continue", 0) == 0) {  │
│       TaskController controller(config_.databasePath);            │
│       return controller.continueTask(request);                    │
│     }                                                             │
└───────────────────────────────────────────────────────────────────┘
```

### 按功能类型选模板

| 功能类型 | 数据层 | 领域层 | 编排层 | Controller | 路线注册位置 |
|---------|--------|--------|--------|-----------|-------------|
| **新路由** (如 continue) | 新增字段/方法 | 新增 AgentLoop 方法 | 新增 Orchestrator 方法 | 新增 Controller 方法 | `handleRequest()` |
| **配置变更通知** | — | 已有 AgentConfiguration | — | 已有 ConfigController | `handleRequest()` |
| **SSE 新事件类型** | — | 新增 SSEGateway::push*() | — | — | — |
| **新工具** | — | — | — | 已有 ToolController | 自动注册 |
| **纯读 API** (如 GET 查询) | 已有 DataAccessFacade | — | — | 新增 Controller 方法 | `handleRequest()` |

### 检查清单

每次新增 API 后，逐项检查：

- [ ] `TaskContext` / 对应数据模型是否新增了必要字段？
- [ ] 核心逻辑类（`AgentLoop` 等）是否新增了对应的 `run*()` 方法？
- [ ] `AgentOrchestrator` 是否暴露了对应的 `start*()` / `continue*()` 等编排方法？
- [ ] `Controller` 是否添加了请求处理方法？（.h 声明 + .cpp 实现）
- [ ] `HttpServer::handleRequest()` 是否注册了新路由？
- [ ] 新 `.cpp` 文件是否加入了 `CMakeLists.txt`？
- [ ] 本文档是否更新了路由表和模块接口表？
- [ ] `POST` 请求体是否在文档中给出了 JSON 示例？

---

## 附录：新旧路由对照

| 类别 | 新路由前缀 | 旧路由 | 状态 |
|------|-----------|--------|------|
| Experts | `/api/v1/experts/*` | — | **🆕 新增** |
| Config | `/api/v1/config/*` | — | **🆕 新增** |
| Tasks (continue) | `/api/v1/tasks/continue` | — | **🆕 新增** |
| Tasks | `/api/v1/tasks/*` | 已有 | 保持 |
| Chat | `/api/v1/chat` | 已有 | **⚠️ 旧 (pending)** |
