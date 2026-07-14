# Expert 配置说明文档

> **版本**：v1.0 | **日期**：2026-07-12
> **关联文件**：`config/experts.json`、`prompts/*.txt`

---

## 一、概述

Expert 系统是 CodePilot 的新一代 Agent 框架。它把 Agent 的业务逻辑完全从 C++ 代码中剥离，通过 JSON 配置文件定义"员工"（Expert）的职责、能力、权限和协作规则。

**核心设计理念**：修改公司架构 = 修改 JSON，无需重编译 loop 逻辑。

---

## 二、配置文件路径

- 主配置文件：`config/experts.json`
- 每个 Expert 的 prompt 模板：`prompts/<expert_name>.txt`（路径由 Expert 的 `prompt_template` 字段指定）

---

## 三、JSON 顶层结构

```json
{
  "experts": [
    { ... expert1 ... },
    { ... expert2 ... },
    { ... expert3 ... }
  ]
}
```

顶层是一个对象，包含 `experts` 数组。每个元素是一个 Expert 定义。

---

## 四、Expert 字段完整说明

### 4.1 基本标识

| 字段 | 类型 | 必填 | 默认值 | 说明 |
|------|------|:---:|--------|------|
| `name` | string | ✅ | — | Expert 的唯一标识符，用于路由和日志。例：`"planner"`、`"executor"` |
| `description` | string | ✅ | — | Expert 的角色描述，会被注入到 prompt 的 `{role}` 占位符中 |
| `prompt_template` | string | ❌ | — | 提示词模板文件路径，如 `"prompts/executor.txt"`。为空时使用内联默认模板 |
| `is_entry` | bool | ❌ | `false` | 是否为入口 Expert。AgentLoop 启动时从 `is_entry=true` 的 Expert 开始执行。有且仅有一个入口 Expert |

### 4.2 上下文配置

| 字段 | 类型 | 必填 | 默认值 | 说明 |
|------|------|:---:|--------|------|
| `context_isolation` | bool | ❌ | `false` | 是否拥有独立的会话上下文。`true` 时，Expert 每次"上班"都会清空 session 历史，不看到之前 Expert 的对话 |
| `context_template` | string | ❌ | 见下方 | 上下文占位符模板。Expert 的 prompt 会按此模板拼接 |

**默认 `context_template`**：
```
{role}\n{goal}\n{plan}\n{summary}\n{tag_protocol}\n{rounds_left}\n{session}
```

**可用占位符**：

| 占位符 | 含义 | 何时填充 |
|--------|------|----------|
| `{role}` | Expert 的 `description` 字段 | 每轮 |
| `{goal}` | 用户原始需求 | 每轮 |
| `{plan}` | 当前 Plan 状态（步骤列表 + 完成状态 + 变更历史） | 每轮 |
| `{summary}` | 当前任务摘要 | 每轮 |
| `{tools_desc}` | 可用工具列表（名称 + 描述） | 仅在 `visible_tools` 非空时 |
| `{tag_protocol}` | 标签协议说明（按 Expert 权限动态生成） | 每轮 |
| `{output_hint}` | 输出格式指引 | 每轮 |
| `{rounds_left}` | 剩余轮次 | 每轮 |
| `{session}` | 当前 Expert 的私有会话历史 | 每轮更新 |

### 4.3 应用层工具

| 字段 | 类型 | 必填 | 默认值 | 说明 |
|------|------|:---:|--------|------|
| `visible_tools` | string[] | ❌ | `[]` | 可调用的工具名称列表。从 ToolSystem 注册表获取工具描述。通过 `<cmd>` 标签调用 |

**示例**：
```json
"visible_tools": ["file.write", "file.read", "shell.run", "git.status"]
```

### 4.4 系统层权限

类比：员工能操作电脑上的软件（`visible_tools`），和员工能否访问公司档案（以下权限），是两码事。

| 字段 | 类型 | 必填 | 默认值 | 说明 |
|------|------|:---:|--------|------|
| `can_modify_plan` | bool | ❌ | `true` | 是否可以使用 `<plan>` 标签修改项目计划 |
| `can_write_summary` | bool | ❌ | `false` | 是否可以使用 `<write to="summary">` 写入任务摘要 |
| `read_global_actively` | bool | ❌ | `false` | 是否可以主动使用 `<read from="global">` 检索历史任务归档 |
| `max_global_rounds` | int | ❌ | `0` | 检索 global 的最大次数（仅在 `read_global_actively=true` 时生效），每次检索消耗 1 轮 |

### 4.5 路由规则

| 字段 | 类型 | 必填 | 默认值 | 说明 |
|------|------|:---:|--------|------|
| `next_rules` | object[] | ❌ | `[]` | 路由规则列表，决定 Expert 下班后的流转目标 |
| `on_fail` | string | ❌ | `""` | Expert 输出 `<fail>` 后的路由目标。为空时默认终止 |

**RouteRule 对象**：

| 字段 | 类型 | 必填 | 默认值 | 说明 |
|------|------|:---:|--------|------|
| `type` | string | ✅ | — | 条件类型：`"tag_exists"` / `"tag_value_match"` / `"plan_state"` / `"default"` |
| `value` | string | ✅ | — | 条件值。`tag_exists` 时是标签名；`plan_state` 时是 `"all_done"` / `"has_failed"` / `"has_pending"` / `"is_empty"` |
| `route_to` | string | ✅ | — | 路由目标 Expert 名称，或内置目标 `"_done"` |
| `priority` | int | ❌ | `0` | 优先级（多个规则同时命中时取最高） |

**路由匹配流程**：
1. 如果 Expert 输出中包含 `<ask>` → 直接路由到 `_user_interrupt`（不经过 next_rules）
2. 如果 Expert 输出中包含 `<fail>` → 使用 `on_fail` 字段
3. 否则遍历 `next_rules`，找出所有匹配的规则，选 priority 最高的
4. 无匹配规则但有 `<done>` 标签 → 默认 `_done`
5. 完全无匹配 → 空字符串（AgentLoop 终止）

### 4.6 容量限制

| 字段 | 类型 | 必填 | 默认值 | 说明 |
|------|------|:---:|--------|------|
| `max_internal_rounds` | int | ❌ | `5` | Expert 内循环最大轮次（"KPI 上限"）。超时未输出 `<done>`/`<fail>`，自动强制 `<fail>` |
| `tool_timeout_seconds` | int | ❌ | `60` | 单个工具调用的超时（秒） |

---

## 五、标签协议速查

Expert 通过 XML 标签与 AgentLoop 通信。以下标签由 AgentLoop 根据 Expert 权限**动态注入 prompt**——Expert 不知道它无权使用的标签的存在。

| 标签 | 触发条件 | 说明 |
|------|----------|------|
| `<cmd>tool_name {...}</cmd>` | `visible_tools` 非空 | 调用应用层工具 |
| `<plan><add>...</add><complete index="0"/><fail index="1" reason="..."/><status/></plan>` | `can_modify_plan=true` | 操作计划 |
| `<write to="summary">...</write>` | `can_write_summary=true` | 写入任务摘要 |
| `<read from="global" max_items="5"/>` | `read_global_actively=true` | 检索全局上下文 |
| `<done>...</done>` | 始终可用 | 正常下班 |
| `<fail>...</fail>` | 始终可用 | 异常下班 |
| `<ask>...</ask>` | 始终可用 | 征求用户意见 |

---

## 六、完整配置示例

### 6.1 单 Expert 最简配置（快速启动调试用）

```json
{
  "experts": [
    {
      "name": "universal_agent",
      "description": "你是一个通用的编程助手，负责完成用户的所有任务。你可以读写文件、执行命令、管理项目。",
      "prompt_template": "",
      "is_entry": true,
      "context_isolation": false,
      "context_template": "{role}\n{goal}\n{plan}\n{summary}\n{tools_desc}\n{tag_protocol}\n{rounds_left}\n{session}",

      "visible_tools": ["file.write", "file.read", "file.list", "shell.run", "git.status", "git.diff"],

      "can_modify_plan": true,
      "can_write_summary": true,
      "read_global_actively": false,

      "next_rules": [
        {"type": "tag_exists", "value": "done", "route_to": "_done", "priority": 10}
      ],
      "on_fail": "_done",

      "max_internal_rounds": 10,
      "tool_timeout_seconds": 60
    }
  ]
}
```

**说明**：只有一个 Expert，它自己做规划 + 执行 + 汇总。适合快速验证框架是否正常工作。

### 6.2 完整 Expert 链（生产级）

```json
{
  "experts": [
    {
      "name": "planner",
      "description": "你是任务规划专家。你的职责是分析用户需求并生成清晰的执行计划。你不执行任何代码或文件操作。",
      "prompt_template": "prompts/planner.txt",
      "is_entry": true,
      "context_isolation": false,

      "visible_tools": [],

      "can_modify_plan": true,
      "can_write_summary": true,
      "read_global_actively": true,
      "max_global_rounds": 3,

      "next_rules": [
        {"type": "tag_exists", "value": "plan", "route_to": "executor", "priority": 10}
      ],
      "on_fail": "summarizer",

      "max_internal_rounds": 3,
      "tool_timeout_seconds": 60
    },
    {
      "name": "executor",
      "description": "你是执行专家。你的职责是按照计划步骤逐一完成：编写代码、运行命令、验证结果。完成后必须更新计划状态。",
      "prompt_template": "prompts/executor.txt",
      "is_entry": false,
      "context_isolation": true,

      "visible_tools": ["file.write", "file.read", "file.list", "file.apply_patch",
                         "shell.run", "git.status", "git.diff"],

      "can_modify_plan": true,
      "can_write_summary": false,
      "read_global_actively": false,

      "next_rules": [
        {"type": "plan_state", "value": "all_done", "route_to": "reviewer", "priority": 10},
        {"type": "tag_exists", "value": "fail", "route_to": "planner", "priority": 5}
      ],
      "on_fail": "planner",

      "max_internal_rounds": 8,
      "tool_timeout_seconds": 60
    },
    {
      "name": "reviewer",
      "description": "你是代码验收专家。你的职责是审查 executor 的工作成果：检查代码正确性、完整性、风格规范。通过的输出 <done>review_pass</done>，不通过的说明原因并输出 <done>review_fail</done>。",
      "prompt_template": "prompts/reviewer.txt",
      "is_entry": false,
      "context_isolation": true,

      "visible_tools": ["file.read"],

      "can_modify_plan": false,
      "can_write_summary": true,
      "read_global_actively": false,

      "next_rules": [
        {"type": "tag_value_match", "value": "review_pass", "route_to": "summarizer", "priority": 10},
        {"type": "tag_value_match", "value": "review_fail", "route_to": "executor", "priority": 5}
      ],
      "on_fail": "summarizer",

      "max_internal_rounds": 3,
      "tool_timeout_seconds": 60
    },
    {
      "name": "summarizer",
      "description": "你是成果汇报专家。你的职责是整理最终成果，用清晰友好的语言向用户汇报完成了什么、创建了哪些文件、验证结果如何。",
      "prompt_template": "prompts/summarizer.txt",
      "is_entry": false,
      "context_isolation": false,

      "visible_tools": [],

      "can_modify_plan": false,
      "can_write_summary": false,
      "read_global_actively": false,

      "next_rules": [
        {"type": "tag_exists", "value": "done", "route_to": "_done", "priority": 10}
      ],
      "on_fail": "_done",

      "max_internal_rounds": 2,
      "tool_timeout_seconds": 60
    }
  ]
}
```

**Expert 链流转**：
```
entry(planner) → executor ↔ reviewer → summarizer → _done
                  ↓                    ↓
               planner(on fail)    summarizer(on fail)
```

### 6.3 Debug 增强配置（每个 Expert 后加调试日志）

如果需要在某个 Expert 完成后查看中间状态，可以在 `next_rules` 中添加 temporary debug expert：

```json
"next_rules": [
  {"type": "plan_state", "value": "all_done", "route_to": "debug_dump", "priority": 10},
  {"type": "plan_state", "value": "all_done", "route_to": "reviewer", "priority": 5}
]
```

---

## 七、Prompt 模板文件格式

Prompt 模板是纯文本文件，使用 `{占位符}` 语法。模板文件路径由 Expert 的 `prompt_template` 字段指定。

**模板占位符由 `context_template` 决定替换顺序**，但以下占位符由 PromptBuilder 硬编码替换：

| 模板中可用占位符 | 替换内容 |
|------------------|----------|
| `{role}` | Expert 的 `description` |
| `{goal}` | 用户原始需求 |
| `{plan}` | `Plan::toPromptFragment()` 输出 |
| `{summary}` | TaskContext 的 summary 字段 |
| `{tools_desc}` | `PromptBuilder::buildToolsDescription()` 输出 |
| `{tag_protocol}` | `PromptBuilder::buildTagProtocol()` 输出 |
| `{output_hint}` | `PromptBuilder::buildOutputHint()` 输出 |
| `{rounds_left}` | 字符串 `"剩余轮次: N"` |
| `{session}` | 首轮为空，后续轮次为对话历史 |

**如果 `prompt_template` 为空字符串**，PromptBuilder 使用内联默认模板：
```
{role}

{goal}

{plan}
{summary}
{tools_desc}{tag_protocol}
{output_hint}
{rounds_left}
```

**创建 prompt 模板文件的建议**：

```
prompts/
├── planner.txt      ← planner 的 prompt 模板
├── executor.txt     ← executor 的 prompt 模板
├── reviewer.txt     ← reviewer 的 prompt 模板
└── summarizer.txt   ← summarizer 的 prompt 模板
```

---

## 八、调试技巧

### 8.1 验证配置是否加载成功

AgentLoop 构造函数加载配置后，`isReady()` 返回 `true` 表示成功。

### 8.2 观察 Expert 链流转

`AgentLoopResult::expertChain` 字段记录了所有经过的 Expert 名称，如 `["planner", "executor", "reviewer", "summarizer"]`。

### 8.3 内循环 Debug 日志

SSE debug 通道会推送每次内循环的轮次信息：
```
[executor] 第1轮, 剩余5轮
[executor] 第2轮, 剩余4轮
工具 [file.write]: 成功: ...
```

### 8.4 快速定位配置错误

- `"config_error"` + "未找到入口 Expert" → 检查是否有 `"is_entry": true`
- `"config_error"` + "路由目标 Expert [...] 未在配置中找到" → 检查 `route_to` 拼写
- Expert 切换超过 20 次 → 可能存在循环路由

---

## 九、变更记录

| 版本 | 日期 | 内容 |
|------|------|------|
| v1.0 | 2026-07-12 | 初稿：完整字段说明、三种配置示例、标签协议速查、调试技巧 |