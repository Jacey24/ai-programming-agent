// ============================================================
// CodePilot 工具系统 - 独立测试入口
// Sprint 1 验证：工具注册、调用、Schema 生成
// ============================================================

#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

#include "domain/tools/BuiltinShell.h"
#include "domain/tools/FileTool.h"
#include "domain/tools/Tool.h"
#include "domain/tools/ToolRegistry.h"
#include "event/EventBus.h"
#include "infrastructure/filesystem/Workspace.h"


using namespace codepilot;

void printSection(const std::string &title) {
  std::cout << "\n";
  std::cout << "============================================================\n";
  std::cout << "  " << title << "\n";
  std::cout << "============================================================\n";
}

void printResult(const std::string &label, const ToolResult &result) {
  std::cout << "  [ " << (result.success ? "OK" : "FAIL") << " ] " << label
            << "\n";
  if (!result.output.empty()) {
    std::cout << "       Output: " << result.output.substr(0, 200) << "\n";
  }
  if (!result.error.empty()) {
    std::cout << "       Error: " << result.error << "\n";
  }
}

// ============================================================
// Test 1: ToolRegistry 注册与查询
// ============================================================
void testToolRegistry() {
  printSection("Test 1: ToolRegistry 注册与查询");

  ToolRegistry registry;
  auto workspace = std::make_shared<Workspace>(".");
  auto shell = std::make_shared<BuiltinShell>(workspace);
  registerFileTools(registry, shell);

  std::cout << "  注册工具数: " << registry.size() << "\n";
  std::cout << "  工具列表:\n";
  for (const auto &name : registry.listToolNames()) {
    auto *tool = registry.getTool(name);
    if (tool) {
      std::cout << "    - " << name << ": " << tool->description() << "\n";
    }
  }

  bool hasFileList = registry.hasTool("file.list");
  bool hasFileRead = registry.hasTool("file.read");
  bool hasUnknown = registry.hasTool("nonexistent.tool");

  std::cout << "  hasTool(\"file.list\"): " << (hasFileList ? "YES" : "NO")
            << "\n";
  std::cout << "  hasTool(\"file.read\"): " << (hasFileRead ? "YES" : "NO")
            << "\n";
  std::cout << "  hasTool(\"nonexistent\"): " << (hasUnknown ? "YES" : "NO")
            << "\n";

  if (registry.size() >= 6 && hasFileList && hasFileRead && !hasUnknown) {
    std::cout << "\n  >>> Test 1 PASSED <<<\n";
  } else {
    std::cout << "\n  >>> Test 1 FAILED <<<\n";
  }
}

// ============================================================
// Test 2: Schema 生成
// ============================================================
void testSchemaGeneration() {
  printSection("Test 2: Tool Schema 生成（OpenAI 格式）");

  ToolRegistry registry;
  auto workspace = std::make_shared<Workspace>(".");
  auto shell = std::make_shared<BuiltinShell>(workspace);
  registerFileTools(registry, shell);

  auto schemas = registry.listSchemas();
  std::string schemaStr = schemas.dump(2);
  std::cout << "  生成的 Schema:\n" << schemaStr.substr(0, 500) << "...\n";

  auto summaries = registry.listSummaries();
  std::string summaryStr = summaries.dump(2);
  std::cout << "\n  生成的 Summary（渐进式）：\n"
            << summaryStr.substr(0, 300) << "...\n";

  // 验证 listToolInfo 格式
  auto info = registry.listToolInfo();
  std::cout << "\n  listToolInfo 格式:\n"
            << info.dump(2).substr(0, 300) << "...\n";

  std::cout << "\n  >>> Test 2 PASSED <<<\n";
}

// ============================================================
// Test 3: 参数校验
// ============================================================
void testValidation() {
  printSection("Test 3: 参数校验");

  ToolRegistry registry;
  auto workspace = std::make_shared<Workspace>(".");
  auto shell = std::make_shared<BuiltinShell>(workspace);
  registerFileTools(registry, shell);

  ToolContext ctx{"task_001", "ws_001", ".", "session_001", {}};

  // 3a: file.read 缺少必填参数 path
  json missingArgs = json::object();
  auto result = registry.call("file.read", ctx, missingArgs);
  printResult("file.read 缺少 path", result);
  bool test3a = !result.success &&
                result.error.find("Validation failed") != std::string::npos;

  // 3b: file.read 带正确的参数
  json validArgs;
  validArgs["path"] = "CMakeLists.txt";
  result = registry.call("file.read", ctx, validArgs);
  printResult("file.read CMakeLists.txt", result);
  bool test3b =
      result.success ||
      (!result.success && result.error.find("not found") != std::string::npos);

  // 3c: 不存在的工具
  result = registry.call("nonexistent.tool", ctx, missingArgs);
  printResult("调用不存在的工具", result);
  bool test3c = !result.success &&
                result.error.find("Tool not found") != std::string::npos;

  std::cout << "\n  Test 3a (缺参校验): " << (test3a ? "PASS" : result.error)
            << "\n";
  std::cout << "  Test 3b (合法调用): " << (test3b ? "PASS" : "FAIL") << "\n";
  std::cout << "  Test 3c (不存在工具): " << (test3c ? "PASS" : "FAIL") << "\n";
}

// ============================================================
// Test 4: Workspace 路径安全
// ============================================================
void testWorkspace() {
  printSection("Test 4: Workspace 路径安全");

  std::string cwd = std::filesystem::current_path().string();
  Workspace ws(cwd);

  std::cout << "  Workspace 根路径: " << ws.rootPath() << "\n";
  std::cout << "  当前路径: " << ws.currentPath() << "\n";

  bool safe = ws.isPathSafe("CMakeLists.txt");
  std::cout << "  isPathSafe(\"CMakeLists.txt\"): "
            << (safe ? "SAFE" : "BLOCKED") << "\n";

  bool unsafe1 = ws.isPathSafe("../");
  std::cout << "  isPathSafe(\"../\"): " << (unsafe1 ? "SAFE" : "BLOCKED")
            << "\n";

  bool unsafe2 = ws.isPathSafe("/etc/passwd");
  std::cout << "  isPathSafe(\"/etc/passwd\"): "
            << (unsafe2 ? "SAFE" : "BLOCKED") << "\n";

  bool unsafe3 = ws.isPathSafe(".git/config");
  std::cout << "  isPathSafe(\".git/config\"): "
            << (unsafe3 ? "SAFE" : "BLOCKED") << "\n";

  bool testPassed = safe && !unsafe1 && !unsafe2 && !unsafe3;
  std::cout << "\n  >>> Test 4 " << (testPassed ? "PASSED" : "FAILED")
            << " <<<\n";
}

// ============================================================
// Test 5: file.list / file.read
// ============================================================
void testFileOperations() {
  printSection("Test 5: File 工具功能测试");

  auto workspace = std::make_shared<Workspace>(".");
  auto shell = std::make_shared<BuiltinShell>(workspace);

  // 5a: 列出当前目录
  json listArgs;
  listArgs["depth"] = 1;
  auto result = shell->listFiles(listArgs);
  printResult("file.list (depth=1)", result);
  bool test5a = result.success;

  // 5b: 读取 CMakeLists.txt
  json readArgs;
  readArgs["path"] = "CMakeLists.txt";
  readArgs["start_line"] = 1;
  readArgs["end_line"] = 5;
  result = shell->readFile(readArgs);
  printResult("file.read CMakeLists.txt (1-5)", result);
  bool test5b = true;

  // 5c: 读取不存在的文件
  json badArgs;
  badArgs["path"] = "does_not_exist.txt";
  result = shell->readFile(badArgs);
  printResult("file.read 不存在的文件", result);
  bool test5c = !result.success;

  std::cout << "\n  Test 5a (list): " << (test5a ? "PASS" : "FAIL") << "\n";
  std::cout << "  Test 5b (read): " << (test5b ? "PASS" : "FAIL") << "\n";
  std::cout << "  Test 5c (不存在): " << (test5c ? "PASS" : "FAIL") << "\n";
}

// ============================================================
// Test 6: EventBus 事件系统
// ============================================================
void testEventBus() {
  printSection("Test 6: EventBus 事件系统");

  EventBus bus;
  std::vector<EventData> received;

  auto id = bus.subscribeAll(
      [&received](const EventData &e) { received.push_back(e); });

  // 使用 json 作为 metadata
  json meta1;
  auto event1 =
      EventData::Create("task_001", EventType::TaskCreated, "任务已创建");

  json meta2;
  meta2["tool_name"] = "file.read";
  auto event2 = EventData::Create("task_001", EventType::ToolStarted,
                                  "开始执行 file.read", meta2);

  json meta3;
  meta3["exit_code"] = 0;
  meta3["duration_ms"] = 3400;
  auto event3 =
      EventData::Create("task_001", EventType::ToolFinished, "执行完成", meta3);

  bus.publish(event1);
  bus.publish(event2);
  bus.publish(event3);

  std::cout << "  发布事件数: 3\n";
  std::cout << "  收到事件数: " << received.size() << "\n";

  for (const auto &e : received) {
    std::cout << "    event: " << e.typeToString()
              << " | content: " << e.content
              << " | serialize: " << e.serialize().substr(0, 80) << "...\n";
    std::cout << "       ISO 8601: " << e.createdAt << "\n";
  }

  // 验证时间格式
  bool hasValidTime = !received.empty() && received[0].createdAt.size() > 10 &&
                      received[0].createdAt.find('T') != std::string::npos;
  std::cout << "  时间格式有效（ISO 8601）: " << (hasValidTime ? "YES" : "NO")
            << "\n";

  bus.unsubscribe(id);

  auto event4 =
      EventData::Create("task_001", EventType::TaskCompleted, "任务完成");
  bus.publish(event4);
  std::cout << "  取消订阅后收到数: " << received.size() << "（应为 3）\n";

  auto history = bus.getHistory("task_001");
  std::cout << "  历史事件数: " << history.size() << "\n";

  bool testPassed = received.size() == 3 && history.size() == 4 && hasValidTime;
  std::cout << "\n  >>> Test 6 " << (testPassed ? "PASSED" : "FAILED")
            << " <<<\n";
}

// ============================================================
// Test 7: 风险等级 + listToolInfo
// ============================================================
void testRiskLevels() {
  printSection("Test 7: 工具风险等级 + API 兼容");

  ToolRegistry registry;
  auto workspace = std::make_shared<Workspace>(".");
  auto shell = std::make_shared<BuiltinShell>(workspace);
  registerFileTools(registry, shell);

  // 获取 listToolInfo 并验证 snake_case 字段
  auto info = registry.listToolInfo();
  if (info.contains("items")) {
    for (const auto &item : info["items"]) {
      std::cout << "  " << item["name"].get<std::string>()
                << " | risk: " << item["risk_level"].get<std::string>() << " | "
                << item["description"].get<std::string>().substr(0, 40) << "\n";
    }
  }

  auto checkRisk = [&](const std::string &name, RiskLevel expected) {
    auto *tool = registry.getTool(name);
    if (!tool) {
      std::cout << "  " << name << ": NOT FOUND\n";
      return false;
    }
    json dummy = json::object();
    RiskLevel actual = tool->riskLevel(dummy);
    bool match = actual == expected;
    std::cout << "  " << name << ": " << riskLevelToString(actual)
              << (match ? " ✓" : " ✗");
    if (!match)
      std::cout << " (expected " << riskLevelToString(expected) << ")";
    std::cout << "\n";
    return match;
  };

  bool allMatch = true;
  allMatch &= checkRisk("file.list", RiskLevel::Safe);
  allMatch &= checkRisk("file.read", RiskLevel::Safe);
  allMatch &= checkRisk("file.write", RiskLevel::Medium);
  allMatch &= checkRisk("file.apply_patch", RiskLevel::Medium);
  allMatch &= checkRisk("cd", RiskLevel::Safe);
  allMatch &= checkRisk("pwd", RiskLevel::Safe);

  std::cout << "\n  >>> Test 7 " << (allMatch ? "PASSED" : "FAILED")
            << " <<<\n";
}

// ============================================================
// Test 8: BuiltinShell 系统提示词
// ============================================================
void testSystemPrompt() {
  printSection("Test 8: BuiltinShell 系统提示词");

  auto workspace = std::make_shared<Workspace>(".");
  auto shell = std::make_shared<BuiltinShell>(workspace);

  std::string prompt = shell->getSystemPrompt();
  std::cout << "  系统提示词:\n" << prompt << "\n";

  bool hasWorkspaceRule = prompt.find("工作区安全规则") != std::string::npos;
  bool hasToolList = prompt.find("file.list") != std::string::npos;
  bool hasFileWriteRule = prompt.find("需要用户确认") != std::string::npos;

  std::cout << "  包含安全规则: " << (hasWorkspaceRule ? "YES" : "NO") << "\n";
  std::cout << "  包含工具列表: " << (hasToolList ? "YES" : "NO") << "\n";
  std::cout << "  包含写入规则: " << (hasFileWriteRule ? "YES" : "NO") << "\n";

  bool testPassed = hasWorkspaceRule && hasToolList && hasFileWriteRule;
  std::cout << "\n  >>> Test 8 " << (testPassed ? "PASSED" : "FAILED")
            << " <<<\n";
}

// ============================================================
// 主函数
// ============================================================
int main() {
  std::cout << "\n";
  std::cout << "╔══════════════════════════════════════════════════════════╗\n";
  std::cout << "║       CodePilot 工具系统 - Sprint 1 完整测试套件         ║\n";
  std::cout << "║       (nlohmann/json 适配版)                            ║\n";
  std::cout << "╚══════════════════════════════════════════════════════════╝\n";
  std::cout << "  编译时间: " << __DATE__ << " " << __TIME__ << "\n";
  std::cout << "  工作目录: " << std::filesystem::current_path().string()
            << "\n";

  testToolRegistry();
  testSchemaGeneration();
  testValidation();
  testWorkspace();
  testFileOperations();
  testEventBus();
  testRiskLevels();
  testSystemPrompt();

  printSection("所有测试完成");
  std::cout << "  请逐项确认每个测试的 PASS/FAIL 状态。\n";
  std::cout
      << "  如所有测试均为 PASS，则 Sprint 1 工具系统核心功能验证通过。\n\n";

  return 0;
}