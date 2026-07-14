# SSEGateway — 全局数据中枢

> **版本**：v1.0 | **日期**：2026-07-12
> **状态**：✅ 已实施
>
> **历史命名说明**：该类原为 SSE 推送专用网关（SSEGateway）。随架构门面化演进，已整合 EventBus 广播、SSE 推送、SQLite 持久化为统一入口，成为 **Core 的唯一数据出口**。命名保留以保持团队认知连续性，职责范围已远超「SSE 网关」。

---

## 一、核心设计

**一个 `push()` 入口 + 两组枚举 = 完全决定数据的三个去向。**

```cpp
void push(taskId, eventType, content, metadata, Channel, Persist);
```

### 自动完成的三个操作

```
Core → push()
       ├── ① EventBus::publish()    → 模块内部广播
       ├── ② broadcastFrame()       → SSE → 前端
       └── ③ saveEvent()            → SQLite（仅 Persist::Always）
```

---

## 二、两组枚举

### Channel（频道 → 决定 `metadata.channel` 字段）

| 枚举值 | 字符串 | 语义 | 前端消费方 |
|--------|--------|------|-----------|
| `Channel::Dialog` | `"dialog"` | LLM 自然语言对话 | 对话面板 |
| `Channel::Status` | `"status"` | 任务生命周期事件 | 状态栏 |
| `Channel::Debug` | `"debug"` | 主循环内部状态、Prompt、工具调用 | 调试面板 |

### Persist（持久化策略 → 决定是否落库）

| 枚举值 | 行为 | 适用场景 |
|--------|------|---------|
| `Persist::Always` | 写入 `task_events` 表，断线重连可回放 | 对话、状态变化、工具调用结果 |
| `Persist::Never` | 不写入 SQLite | 流式片段（chunk）、进度、心跳 |

---

## 三、便捷别名一览

所有别名内部调 `push()`，**行为完全等价**，仅提供更简洁的调用语法：

| 别名 | 内部等价于 | 语义 |
|------|-----------|------|
| `pushDialog(t,c,m)` | `push(..., AgentMessage, Channel::Dialog, Always)` | 对话消息 |
| `pushStatus(t,c,e,m)` | `push(..., e, Channel::Status, Always)` | 状态变化 |
| `pushDebug(t,c,e,m)` | `push(..., e, Channel::Debug, Always)` | 调试信息 |
| `pushMessage(t,c,m)` | = `pushDialog(t,c,m)` | 对话消息别名 |
| `pushToolStarted(t,n,m)` | `push(..., ToolStarted, Channel::Debug, Always)` | 工具开始 |
| `pushToolOutput(t,c,m)` | `push(..., ToolOutput, Channel::Debug, Always)` | 工具输出 |
| `pushToolFinished(t,c,m)` | `push(..., ToolFinished, Channel::Debug, Always)` | 工具完成 |
| `pushProgress(t,i,total,a,m)` | `push(..., AgentMessage, Channel::Status, **Never**)` | 进度推送 |
| `pushStream(t,chunk,last,full)` | `agent_message_chunk` + `agent_message`（last=true 时） | 流式输出 |

**带 `*` 的参数**：t=taskId, c=content, m=metadata, e=eventType, n=toolName, i=current, a=action

---

## 四、调用准则

### 何时用哪个

| 使用场景 | 代码 | 说明 |
|---------|------|------|
| 需要实时 + EventBus + 持久化 | `push(..., Always)` | 绝大多数情况 |
| 仅需实时，无需落库 | `push(..., Never)` | 瞬态数据 |
| 流式 LLM 回复 | `pushStream()` | 自动处理 chunk/last |
| 用别名还是直接 push() | 新代码优先 `push()` | 旧代码兼容用别名 |

### 典型调用示例

```cpp
// 记录规划结果 → 调试面板 + 落库
SSEGateway::getInstance().push(
    taskId, EventType::AgentMessage, planJson,
    json{{"source", "planner"}, {"stage", "planning"}},
    SSEGateway::Channel::Debug,
    SSEGateway::Persist::Always
);

// 推送进度 → 状态栏 + 不落库（瞬态）
SSEGateway::getInstance().push(
    taskId, EventType::AgentMessage, "2/5",
    json{{"progress", {{"current", 2}, {"total", 5}}}},
    SSEGateway::Channel::Status,
    SSEGateway::Persist::Never
);

// 流式 LLM 回复（用便捷别名）
gateway.pushStream(taskId, "正在", false);
gateway.pushStream(taskId, "构建方案...", true, "正在构建方案...");
```

---

## 五、数据流架构位置

```
┌──────────────────────────────────────────────────┐
│                  Core (Agent)                     │
│                                                    │
│  唯一依赖：SSEGateway                             │
│  （不再直接调用 DataAccessFacade）                │
└────────────────────────┬─────────────────────────┘
                         │ push()
                         ▼
┌──────────────────────────────────────────────────┐
│                  SSEGateway                        │
├────────────────┬────────────────┬─────────────────┤
│  EventBus      │  broadcastFrame │  saveEvent()   │
│  → 模块内部    │  → 前端         │  → SQLite       │
│  (同步)        │  (实时)         │  (仅 Always)    │
└────────────────┴────────────────┴─────────────────┘
```

---

## 六、与 DataAccessFacade 的关系

| 对比维度 | SSEGateway（数据中枢） | DataAccessFacade（存储门面） |
|---------|----------------------|---------------------------|
| 对 Core 可见 | ✅ Core 唯一数据出口 | ❌ Core 不再调（已消除 6 处 appendLog） |
| 职责 | 路由 + 分发 + 选择性子 | 纯粹的 CRUD |
| 使用者 | Agent、AgentRunner、TaskOrchestrator | SSEGateway 内部、TaskController（查询用） |
| 是否可独立使用 | 否（持 EventBus + dataFacade 指针） | 是 |

**核心原则**：Core 的所有数据操作统一经过 SSEGateway，DataAccessFacade 退居为 SSEGateway 的内部依赖。

---

## 七、SSE 协议事件清单

| event: 标签 | 推送时机 | channel |
|-------------|---------|---------|
| `agent_message` | LLM 回复/对话 | dialog |
| `agent_message_chunk` | 流式片段 | dialog |
| `task_planning` | 规划开始 | status |
| `task_completed` | 任务完成 | status |
| `task_failed` | 任务失败 | status |
| `task_cancelled` | 任务取消 | status |
| `tool_started` | 工具调用开始 | debug |
| `tool_output` | 工具输出 | debug |
| `tool_finished` | 工具完成 | debug |
| `file_changed` | 文件变更 | status |
| `progress` | 进度更新 | status |
| `stream_end` | 连接关闭 | - |
| `: ping` | 心跳 | - |

---

## 八、快速参考

```cpp
// 0. 获取单例
auto &gateway = SSEGateway::getInstance();

// 1. 推送事件（完整控制）
gateway.push(taskId, eventType, content, metadata, channel, persist);

// 2. 推送事件（便捷别名）
gateway.pushDebug(taskId, content, eventType, metadata);
gateway.pushStatus(taskId, content, eventType, metadata);
gateway.pushDialog(taskId, content, metadata);

// 3. 流式推送
gateway.pushStream(taskId, chunk, isLast, fullContent);

// 4. 进度推送
gateway.pushProgress(taskId, current, total, action, metadata);

// 5. SSE 长连接
gateway.streamTaskEvents(sendCallback, taskId);