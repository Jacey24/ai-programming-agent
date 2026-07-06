# Astral Skill Protocol

## Overview

Astral uses an XML-based protocol for communication between the Agent, Dispatcher, and Expert skills. This document defines the protocol format used throughout the system.

## Roles

- **Dispatcher**: The entry skill that plans tasks based on user input
- **Expert**: Individual skill that executes specific tasks
- **Agent**: The runtime engine orchestrating the flow
- **Chat**: A special skill for summarizing results to the user

## Flow

```
User Input → Dispatcher (plan) → Agent → Expert 1 (loop until DONE)
                                     → Expert 2 (loop until DONE)
                                     → Chat (summarize)
                                     → Output to User
```

## 1. Task Planning (Dispatcher → Agent)

The Dispatcher outputs either:

### Plan Format (when task execution is needed)

```xml
<plan>
  <item skill="memory" task="List all nodes, create classification" />
  <item skill="chat" task="Report results to user" />
</plan>
```

### Direct Chat Format (for simple conversations)

```xml
<chat>What the user wants to talk about</chat>
```

### Item Attributes

| Attribute | Type   | Description                              |
|-----------|--------|------------------------------------------|
| skill     | string | Name of the expert skill to invoke       |
| task      | string | Task description passed to the expert    |
| fallback  | bool   | (optional) Only execute if previous failed |

## 2. Expert Execution (Expert → Agent)

Experts communicate using `<cmd>` tags and final markers (`DONE` / `FAIL`):

### Executing a Command

```xml
<cmd>REMEMBER test_node This is a test node</cmd>
```

The agent executes the command and returns the result:

```
[RESULT: Remembered: test_node]
```

### Task Completion (Success)

```
DONE Task summary describing what was accomplished
```

### Task Completion (Failure)

```
FAIL Reason for failure — agent may retry or skip
```

### Expert Loop Flow

1. Agent sends task + context to expert
2. Expert outputs `<cmd>...</cmd>` → Agent executes → returns `[RESULT: ...]`
3. Expert continues until it outputs either:
   - `DONE <summary>` — task succeeded
   - `FAIL <reason>` — task failed (agent marks `previous_succeeded = false`)
4. If expert fails to complete within `max_expert_calls`, task is automatically failed
5. All CMD results are collected and injected into `main_context` for downstream visibility

### Dual Marker Protocol

| Marker | Meaning  | Agent Response                                       |
|--------|----------|------------------------------------------------------|
| DONE   | Success  | Task summary captured, `previous_succeeded = true`   |
| FAIL   | Failure  | Failure reason captured, `previous_succeeded = false`|

Both markers must appear at the start of a line (or after a newline) for proper detection.

## 3. JSON Output Format (External Skills)

All external executable skills must output JSON with this structure:

```json
{
  "ok": true,
  "msg": "Human-readable message",
  "data": {}
}
```

| Field  | Type   | Description                                |
|--------|--------|--------------------------------------------|
| ok     | bool   | Whether the operation succeeded            |
| msg    | string | Human-readable result message              |
| data   | object | Optional structured data for the caller    |

## 4. Skill Configuration Format

Each skill is defined by a JSON file in the `skills/` directory:

```json
{
  "name": "skill_name",
  "display_name": "Display Name",
  "entry": false,
  "exe": "skills/skill_exe.exe",
  "temperature": 0.3,
  "prompt": "System prompt for the AI...",
  "transfer_to": ["chat"],
  "commands": {
    "CMD_NAME": ""
  },
  "ctx": {
    "isolated": true
  },
  "exec": {
    "max_loop_rounds": 8,
    "max_repeat_cmds": 3,
    "fail_strategy": 0,
    "critical": 0
  }
}
```

### Configuration Fields

| Field              | Type   | Description                                                      |
|--------------------|--------|------------------------------------------------------------------|
| name               | string | Unique skill identifier                                          |
| display_name       | string | Human-readable name                                              |
| entry              | bool   | If true, this is the dispatcher entry point                      |
| exe                | string | Path to external executable (empty for AI-only)                  |
| temperature        | number | AI model temperature (0.0 - 2.0)                                 |
| prompt             | string | System prompt for the AI                                         |
| transfer_to        | array  | Skills this skill can delegate to                                |
| commands           | object | Map of command names this skill registers                        |
| ctx.isolated       | bool   | true=独立上下文（memory/calc）, false=全局上下文（chat/dispatcher）|
| exec               | object | Execution control parameters (see below)                         |

### exec Parameters

| Field             | Type   | Default | Description                                               |
|-------------------|--------|---------|-----------------------------------------------------------|
| max_loop_rounds   | int    | 8       | 最大递归轮次，单次任务内防死锁                              |
| max_repeat_cmds   | int    | 3       | 连续相同指令上限，超限触发失败                              |
| fail_strategy     | int    | 0       | 0=总结失败原因, >0=完整重试次数（含max_loop_rounds内循环）   |
| critical          | int    | 0       | 0=继续下个task, 1=FAIL时完全中止全部任务并转chat总结        |

## 5. Context Injection

The Agent injects context into expert prompts:

```
[TASK: <task_description>]
[已完成步骤]
- skill1: summary1
- skill2: summary2
[CONTEXT: <main_context>]
```

For Chat summarization:

```
[已完成的任务]
- memory: Created 3 nodes
[CONTEXT: <main_context>]
[用户原话: <original_user_input>]
```

## 6. Direct Command Routing

Users can prefix commands with `/` to bypass AI routing:

```
> /REMEMBER test Hello world