# Astral 模块拆分说明

## 拆分原则
- **不修改任何关键功能逻辑**
- 按功能领域拆分单文件，保持接口兼容
- 紧耦合功能在同一文件内保留，但通过代码布局和注释标注配合关系
- 原始 agent.cpp 文件中内联的辅助函数与核心方法分离

## 拆分概览

### main.cpp → main.cpp + core/cli_builtins.cpp
- `main.cpp`: 保留入口逻辑、初始化流程、REPL循环骨架
- `core/cli_builtins.cpp`: 提取所有 `/` 命令的注册逻辑（help/ls/plan/act/debug/cd/home/mask/setmask/approve/block/unblock/resume/workfolder）

### core/agent.cpp → 按职责拆分
- `core/agent_types.hpp`: 数据结构定义（TaskResult, PlanEntry, AgentConfig, ExecutePlanResult, AgentResult）
- `core/agent_policy.cpp`: 掩码系统 + 工作目录系统（get_mask/set_mask/mask_status/load_masks/save_masks + 所有workfolder/home相关）
- `core/agent_exec.cpp`: 执行引擎（execute_plan, invoke_analyser, exec_cmd, make_call, build_expert_context, 解析辅助函数）
- `core/agent_interact.cpp`: 交互层（process, plan_interact, execute_outline, execute_resume, 日志辅助函数）

### 拆分后文件结构
```
Astral/source/
├── main.cpp                  — 入口，REPL循环骨架（精简）
│
Astral/core/
├── agent.hpp                 — Agent类声明（接口不变，注释增强）
├── agent_types.hpp           — 数据结构、枚举定义（NEW）
├── agent_policy.cpp          — 掩码系统 + 工作目录（从agent.cpp拆分）
├── agent_exec.cpp            — 执行引擎（从agent.cpp拆分）
├── agent_interact.cpp        — 交互层（从agent.cpp拆分）
├── shell.hpp / shell.cpp     — 命令路由（不变）
├── skill_loader.hpp / .cpp   — 技能加载（不变）
├── util.hpp                  — 工具函数（不变）
├── cli_builtins.cpp          — CLI命令注册（从main.cpp提取，NEW）
│
Astral/runtime/
├── api_client.hpp / .cpp     — HTTP API客户端（不变）
├── context_manager.hpp / .cpp — 上下文管理器（标注：旧代码，agent未使用）
├── output_formatter.hpp / .cpp — 输出格式化（不变）
├── xml_protocol.hpp / .cpp    — XML协议解析（不变）
│
Astral/skill_src/             — 技能实现（不变）
Astral/skills/                — 技能配置（不变）
Astral/protocol/              — 协议文档（不变）
```

## 配合关系说明
- `core/` 四个agent文件共享 `agent.hpp` 声明的 `Agent` 类，通过成员函数分布在不同cpp中实现
- `cli_builtins.cpp` 依赖 `agent.hpp`、`shell.hpp`，输出到 `main.cpp` 的lambda闭包
- `agent_policy` 依赖 `shell.hpp` 的 query 方法
- `agent_exec` 依赖 `api_client.hpp`、`xml_protocol.hpp`、`util.hpp`
- `agent_interact` 依赖 `agent_exec` (execute_plan) 和 `agent_policy` (get_mask等)
- `context_manager.hpp` 保留但标注为旧代码——agent.cpp中已内联实现上下文管理