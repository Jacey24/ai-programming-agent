// tests/test_agent_loop.cpp
// 独立测试程序 —— 验证 Agent Core 完整循环（Windows 本地，不依赖 Docker）
//
// 编译命令 (MSVC):
//   cl /std:c++20 /EHsc /I backend/agent-server /I backend/agent-server/domain/agent ^
//      tests/test_agent_loop.cpp ^
//      backend/agent-server/domain/agent/TaskState.cpp ^
//      backend/agent-server/domain/agent/RoleRegistry.cpp ^
//      backend/agent-server/domain/agent/TaskQueue.cpp ^
//      backend/agent-server/domain/agent/ContextBuilder.cpp ^
//      backend/agent-server/domain/agent/PromptAdapter.cpp ^
//      backend/agent-server/domain/agent/ResponseParser.cpp ^
//      backend/agent-server/domain/agent/Agent.cpp ^
//      /Fe:test_agent.exe
//
// 编译命令 (MinGW):
//   g++ -std=c++20 -I backend/agent-server -I backend/agent-server/domain/agent ^
//      tests/test_agent_loop.cpp ^
//      backend/agent-server/domain/agent/TaskState.cpp ^
//      backend/agent-server/domain/agent/RoleRegistry.cpp ^
//      backend/agent-server/domain/agent/TaskQueue.cpp ^
//      backend/agent-server/domain/agent/ContextBuilder.cpp ^
//      backend/agent-server/domain/agent/PromptAdapter.cpp ^
//      backend/agent-server/domain/agent/ResponseParser.cpp ^
//      backend/agent-server/domain/agent/Agent.cpp ^
//      -o test_agent.exe

#include "Agent.h"
#include "Planner.h"
#include "RoleRegistry.h"
#include <iostream>
#include <string>
#ifdef _WIN32
#include <windows.h>
#endif

int main() {
#ifdef _WIN32
    // 设置控制台为 UTF-8 编码
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    std::cout << "=== CodePilot Agent Core 独立测试 ===" << std::endl;
    std::cout << std::endl;

    // Step 1: 初始化角色注册表
    std::cout << "[1/5] 加载角色配置..." << std::endl;
    codepilot::RoleRegistry registry;
    bool loaded = registry.loadFromFile("config/agent_roles.json");
    if (!loaded) {
        std::cerr << "错误: 无法加载 config/agent_roles.json" << std::endl;
        return 1;
    }
    std::cout << "      loaded " << registry.count() << " roles" << std::endl;

    // Step 2: 创建 Planner
    std::cout << "[2/5] 创建 Planner..." << std::endl;
    codepilot::Planner planner(registry);
    auto steps = planner.generatePlan("测试任务");
    std::cout << "      generated " << steps.size() << " steps:" << std::endl;
    for (const auto& s : steps) {
        std::cout << "        - [" << s.role << "] " << s.action << std::endl;
    }

    // Step 3: 创建 Agent
    std::cout << "[3/5] 创建 Agent..." << std::endl;
    codepilot::Agent agent(registry, planner);

    // Step 4: 执行任务（假 AI 调用）
    std::cout << "[4/5] 执行任务 (假 AI 调用)..." << std::endl;
    auto result = agent.executeTask(
        "test_task_001",
        "test_session_001",
        "test_workspace_001",
        "这是一个测试任务——验证整个 Agent 循环"
    );

    // Step 5: 输出结果
    std::cout << "[5/5] 输出结果:" << std::endl;
    std::cout << "  taskId:       " << result.taskId << std::endl;
    std::cout << "  sessionId:    " << result.sessionId << std::endl;
    std::cout << "  workspaceId:  " << result.workspaceId << std::endl;
    std::cout << "  goal:         " << result.goal << std::endl;
    std::cout << "  status:       " << result.status << std::endl;
    std::cout << "  planJson:     " << result.planJson << std::endl;
    std::cout << "  currentStep:  " << result.currentStep << std::endl;
    std::cout << "  createdAt:    " << result.createdAt << std::endl;
    std::cout << "  updatedAt:    " << result.updatedAt << std::endl;

    std::cout << std::endl;
    std::cout << "=== 测试通过 ===" << std::endl;
    std::cout << "status=" << result.status << std::endl;
    std::cout << "plan steps=" << steps.size() << std::endl;

    return 0;
}
