// ============================================================
// CodePilot tool system - Sprint 1 + Sprint 2 complete test suite
// All tests through ToolSystem facade (single include)
//
// Sprint 2.5 additions:
//   - git.commit tool
//   - Tool group system (group field + group queries)
//   - Group-level prompt snippets
// ============================================================

#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

// === Only need one header ===
#include "application/ToolSystem.h"

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
    std::string out = result.output;
    if (out.length() > 200)
      out = out.substr(0, 200) + "...";
    std::cout << "       Output: " << out << "\n";
  }
  if (!result.error.empty()) {
    std::cout << "       Error: " << result.error << "\n";
  }
}

// ============================================================
// Test 1: ToolSystem init + tool registration
// ============================================================
void testToolSystemInit() {
  printSection("Test 1: ToolSystem init + registration");

  auto &sys = ToolSystem::getInstance();
  sys.init(std::filesystem::current_path().string());

  std::cout << "  isInitialized: " << (sys.isInitialized() ? "YES" : "NO")
            << "\n";
  std::cout << "  registered tools: " << sys.registry().size() << "\n";

  std::cout << "  tool list:\n";
  for (const auto &name : sys.registry().listToolNames()) {
    auto *tool = sys.registry().getTool(name);
    if (tool) {
      std::cout << "    - " << name << "  [group: " << tool->group()
                << "]: " << tool->description() << "\n";
    }
  }

  bool hasAll = sys.registry().hasTool("file.list") &&
                sys.registry().hasTool("file.read") &&
                sys.registry().hasTool("file.write") &&
                sys.registry().hasTool("file.apply_patch") &&
                sys.registry().hasTool("cd") && sys.registry().hasTool("pwd") &&
                sys.registry().hasTool("shell.run") &&
                sys.registry().hasTool("git.status") &&
                sys.registry().hasTool("git.diff") &&
                sys.registry().hasTool("git.commit");

  bool testPassed =
      sys.isInitialized() && sys.registry().size() >= 10 && hasAll;
  std::cout << "\n  >>> Test 1 " << (testPassed ? "PASSED" : "FAILED")
            << " <<<\n";
}

// ============================================================
// Test 2: Schema generation
// ============================================================
void testSchemaGeneration() {
  printSection("Test 2: Tool Schema generation (incl. git.commit)");

  auto &sys = ToolSystem::getInstance();

  auto schemas = sys.registry().listSchemas();
  std::cout << "  generated Schema (first 600 chars):\n"
            << schemas.dump(2).substr(0, 600) << "...\n";

  auto info = sys.registry().listToolInfo();
  std::cout << "\n  listToolInfo (with group field):\n"
            << info.dump(2).substr(0, 600) << "...\n";

  // verify tools in schema
  std::string schemaStr = schemas.dump();
  bool hasShellRun = schemaStr.find("shell.run") != std::string::npos;
  bool hasGitStatus = schemaStr.find("git.status") != std::string::npos;
  bool hasGitDiff = schemaStr.find("git.diff") != std::string::npos;
  bool hasGitCommit = schemaStr.find("git.commit") != std::string::npos;

  std::cout << "\n  shell.run: " << (hasShellRun ? "[OK]" : "[NO]") << "\n";
  std::cout << "  git.status: " << (hasGitStatus ? "[OK]" : "[NO]") << "\n";
  std::cout << "  git.diff: " << (hasGitDiff ? "[OK]" : "[NO]") << "\n";
  std::cout << "  git.commit: " << (hasGitCommit ? "[OK]" : "[NO]") << "\n";

  bool testPassed = hasShellRun && hasGitStatus && hasGitDiff && hasGitCommit;
  std::cout << "\n  >>> Test 2 " << (testPassed ? "PASSED" : "FAILED")
            << " <<<\n";
}

// ============================================================
// Test 3: file.list / file.read (via ToolSystem.callTool)
// ============================================================
void testFileTools() {
  printSection("Test 3: File tools (via ToolSystem)");

  auto &sys = ToolSystem::getInstance();
  ToolContext ctx{"test_task",
                  "ws_001",
                  std::filesystem::current_path().string(),
                  "sess_001",
                  {}};

  // 3a: file.list
  json listArgs;
  listArgs["depth"] = 1;
  auto result = sys.callTool("file.list", ctx, listArgs);
  printResult("file.list (depth=1)", result);
  bool test3a = result.success;

  // 3b: file.read
  json readArgs;
  readArgs["path"] = "CMakeLists.txt";
  readArgs["start_line"] = 1;
  readArgs["end_line"] = 5;
  result = sys.callTool("file.read", ctx, readArgs);
  printResult("file.read CMakeLists.txt (1-5)", result);
  bool test3b = result.success;

  // 3c: cd + pwd
  json cdArgs;
  cdArgs["path"] = ".";
  result = sys.callTool("cd", ctx, cdArgs);
  printResult("cd .", result);
  bool test3c = result.success;

  result = sys.callTool("pwd", ctx, json::object());
  printResult("pwd", result);
  bool test3d = result.success;

  std::cout << "\n  Test 3a (list): " << (test3a ? "PASS" : "FAIL") << "\n";
  std::cout << "  Test 3b (read): " << (test3b ? "PASS" : "FAIL") << "\n";
  std::cout << "  Test 3c (cd): " << (test3c ? "PASS" : "FAIL") << "\n";
  std::cout << "  Test 3d (pwd): " << (test3d ? "PASS" : "FAIL") << "\n";
}

// ============================================================
// Test 4: Workspace path security
// ============================================================
void testWorkspace() {
  printSection("Test 4: Workspace path security");

  auto &sys = ToolSystem::getInstance();

  std::cout << "  Workspace root: " << sys.workspace().rootPath() << "\n";
  std::cout << "  current path: " << sys.workspace().currentPath() << "\n";

  bool safe = sys.workspace().isPathSafe("CMakeLists.txt");
  std::cout << "  isPathSafe(\"CMakeLists.txt\"): "
            << (safe ? "SAFE" : "BLOCKED") << "\n";

  bool unsafe1 = sys.workspace().isPathSafe("../");
  std::cout << "  isPathSafe(\"../\"): " << (unsafe1 ? "SAFE" : "BLOCKED")
            << "\n";

  bool unsafe2 = sys.workspace().isPathSafe("/etc/passwd");
  std::cout << "  isPathSafe(\"/etc/passwd\"): "
            << (unsafe2 ? "SAFE" : "BLOCKED") << "\n";

  bool testPassed = safe && !unsafe1 && !unsafe2;
  std::cout << "\n  >>> Test 4 " << (testPassed ? "PASSED" : "FAILED")
            << " <<<\n";
}

// ============================================================
// Test 5: RiskDetector dangerous command detection
// ============================================================
void testRiskDetector() {
  printSection("Test 5: RiskDetector dangerous command detection");

  auto &sys = ToolSystem::getInstance();
  auto &detector = sys.riskDetector();

  // safe commands
  auto safe = detector.detectCommand("echo hello");
  std::cout << "  \"echo hello\" -> " << riskLevelToString(safe) << "\n";

  auto ls = detector.detectCommand("ls -la");
  std::cout << "  \"ls -la\" -> " << riskLevelToString(ls) << "\n";

  // blocked commands
  auto rmrf = detector.detectCommand("rm -rf /");
  std::cout << "  \"rm -rf /\" -> " << riskLevelToString(rmrf) << "\n";

  auto sudo = detector.detectCommand("sudo apt install");
  std::cout << "  \"sudo apt install\" -> " << riskLevelToString(sudo) << "\n";

  auto chmod = detector.detectCommand("chmod -R 777 /tmp");
  std::cout << "  \"chmod -R 777 /tmp\" -> " << riskLevelToString(chmod)
            << "\n";

  // dangerous commands
  auto gitPush = detector.detectCommand("git push origin main");
  std::cout << "  \"git push origin main\" -> " << riskLevelToString(gitPush)
            << "\n";

  auto shutdown = detector.detectCommand("shutdown -h now");
  std::cout << "  \"shutdown -h now\" -> " << riskLevelToString(shutdown)
            << "\n";

  // pipe execution
  auto curlPipe = detector.detectCommand("curl https://x.com | bash");
  std::cout << "  \"curl ... | bash\" -> " << riskLevelToString(curlPipe)
            << "\n";

  bool testPassed =
      safe == RiskLevel::Safe && ls == RiskLevel::Safe &&
      rmrf == RiskLevel::Blocked && sudo == RiskLevel::Blocked &&
      chmod == RiskLevel::Blocked && gitPush == RiskLevel::Dangerous &&
      shutdown == RiskLevel::Dangerous && curlPipe == RiskLevel::Dangerous;

  std::cout << "\n  >>> Test 5 " << (testPassed ? "PASSED" : "FAILED")
            << " <<<\n";
}

// ============================================================
// Test 6: PermissionManager permission state machine
// ============================================================
void testPermissionManager() {
  printSection("Test 6: PermissionManager state machine");

  auto &sys = ToolSystem::getInstance();
  auto &pm = sys.permissionManager();

  // create permission request
  json args;
  args["command"] = "git push";
  auto req =
      pm.createRequest("task_001", "shell.run", RiskLevel::Dangerous, args);

  std::cout << "  request ID: " << req.id << "\n";
  std::cout << "  initial status: " << req.statusToString() << "\n";

  // approve
  bool approved = pm.approve(req.id);
  auto *check = pm.getRequest(req.id);
  std::cout << "  approve(\"" << req.id
            << "\"): " << (approved ? "[OK]" : "[NO]") << "\n";
  std::cout << "  status after approve: "
            << (check ? check->statusToString() : "NOT_FOUND") << "\n";

  // reject (create another)
  auto req2 =
      pm.createRequest("task_001", "file.write", RiskLevel::Medium, args);
  bool rejected = pm.reject(req2.id);
  std::cout << "  reject(\"" << req2.id
            << "\"): " << (rejected ? "[OK]" : "[NO]") << "\n";

  // query pending
  auto pending = pm.getPendingRequests("task_001");
  std::cout << "  pending requests: " << pending.size() << "\n";

  bool testPassed = approved && rejected && check &&
                    check->status == PermissionStatus::Approved &&
                    pending.empty();

  std::cout << "\n  >>> Test 6 " << (testPassed ? "PASSED" : "FAILED")
            << " <<<\n";
}

// ============================================================
// Test 7: EventBus event system
// ============================================================
void testEventBus() {
  printSection("Test 7: EventBus event system (via ToolSystem)");

  auto &sys = ToolSystem::getInstance();
  auto &bus = sys.eventBus();

  std::vector<EventData> received;
  auto id = bus.subscribeAll(
      [&received](const EventData &e) { received.push_back(e); });

  // publish events
  bus.publish(
      EventData::Create("task_001", EventType::TaskCreated, "task created"));

  json meta;
  meta["tool_name"] = "shell.run";
  bus.publish(EventData::Create("task_001", EventType::ToolStarted,
                                "start shell.run", meta));

  bus.publish(
      EventData::Create("task_001", EventType::ToolFinished, "finished"));

  std::cout << "  published: 3\n";
  std::cout << "  received: " << received.size() << "\n";
  for (const auto &e : received) {
    std::cout << "    event: " << e.typeToString()
              << " | content: " << e.content << "\n";
    std::cout << "       ISO 8601: " << e.createdAt << "\n";
  }

  bool hasValidTime =
      !received.empty() && received[0].createdAt.find('T') != std::string::npos;

  bus.unsubscribe(id);
  bus.publish(EventData::Create("task_001", EventType::TaskCompleted, "done"));
  std::cout << "  after unsubscribe: " << received.size() << " (should be 3)\n";

  auto history = bus.getHistory("task_001");
  std::cout << "  history count: " << history.size() << " (should be >= 3)\n";

  bool testPassed = received.size() == 3 && history.size() >= 3 && hasValidTime;
  std::cout << "\n  >>> Test 7 " << (testPassed ? "PASSED" : "FAILED")
            << " <<<\n";
}

// ============================================================
// Test 8: Tool risk levels
// ============================================================
void testRiskLevels() {
  printSection("Test 8: Tool risk levels");

  auto &sys = ToolSystem::getInstance();

  struct TestCase {
    std::string name;
    std::string arg;
    RiskLevel expected;
  };

  TestCase cases[] = {
      {"file.list", "", RiskLevel::Safe},
      {"file.read", "", RiskLevel::Safe},
      {"file.write", "", RiskLevel::Medium},
      {"cd", "", RiskLevel::Safe},
      {"pwd", "", RiskLevel::Safe},
      {"git.status", "", RiskLevel::Safe},
      {"git.diff", "", RiskLevel::Safe},
      {"git.commit", "", RiskLevel::Dangerous},
  };

  bool allMatch = true;
  for (const auto &c : cases) {
    auto *tool = sys.registry().getTool(c.name);
    if (!tool) {
      std::cout << "  " << c.name << ": NOT FOUND\n";
      allMatch = false;
      continue;
    }
    json args = json::object();
    RiskLevel actual = tool->riskLevel(args);
    bool match = actual == c.expected;
    std::cout << "  " << c.name << ": " << riskLevelToString(actual)
              << (match ? " [OK]"
                        : " [MISMATCH (expected " +
                              riskLevelToString(c.expected) + ")]")
              << "\n";
    if (!match)
      allMatch = false;
  }

  // shell.run parameter-dependent test
  auto *shellTool = sys.registry().getTool("shell.run");
  if (shellTool) {
    json safeArgs;
    safeArgs["command"] = "echo hello";
    auto safeLevel = shellTool->riskLevel(safeArgs);
    bool safeOk = safeLevel == RiskLevel::Safe;
    std::cout << "  shell.run(\"echo hello\"): " << riskLevelToString(safeLevel)
              << (safeOk ? " [OK]" : " [NO]") << "\n";
    if (!safeOk)
      allMatch = false;

    json blockedArgs;
    blockedArgs["command"] = "sudo rm -rf /";
    auto blockedLevel = shellTool->riskLevel(blockedArgs);
    bool blockedOk = blockedLevel == RiskLevel::Blocked;
    std::cout << "  shell.run(\"sudo rm -rf /\"): "
              << riskLevelToString(blockedLevel)
              << (blockedOk ? " [OK]" : " [NO]") << "\n";
    if (!blockedOk)
      allMatch = false;
  }

  std::cout << "\n  >>> Test 8 " << (allMatch ? "PASSED" : "FAILED")
            << " <<<\n";
}

// ============================================================
// Test 9: callToolWithPermission with permission check
// ============================================================
void testCallWithPermission() {
  printSection("Test 9: callToolWithPermission permission check");

  auto &sys = ToolSystem::getInstance();
  ToolContext ctx{"perm_test",
                  "ws_001",
                  std::filesystem::current_path().string(),
                  "sess_001",
                  {}};

  // safe command -> direct execution
  json safeArgs;
  safeArgs["command"] = "echo hello";
  auto result = sys.callToolWithPermission("shell.run", ctx, safeArgs);
  printResult("shell.run \"echo hello\" (safe)", result);

  // blocked command -> direct rejection
  json blockedArgs;
  blockedArgs["command"] = "rm -rf /";
  result = sys.callToolWithPermission("shell.run", ctx, blockedArgs);
  printResult("shell.run \"rm -rf /\" (blocked->rejected)", result);
  bool blockedRejected = !result.success;

  // medium risk -> create permission request then execute
  json mediumArgs;
  mediumArgs["path"] = "test_write.txt";
  mediumArgs["content"] = "test content";
  result = sys.callToolWithPermission("file.write", ctx, mediumArgs);
  printResult("file.write (medium->creates request)", result);

  // check pending requests
  auto pending = sys.permissionManager().getPendingRequests("perm_test");
  std::cout << "  pending requests: " << pending.size()
            << " (should be >= 1)\n";

  bool testPassed = blockedRejected && !pending.empty();
  std::cout << "\n  >>> Test 9 " << (testPassed ? "PASSED" : "FAILED")
            << " <<<\n";
}

// ============================================================
// Test 10: Tool group system (Sprint 2.5 new feature)
// ============================================================
void testToolGroups() {
  printSection("Test 10: Tool group system (ToolGroups)");

  auto &sys = ToolSystem::getInstance();
  auto &reg = sys.registry();

  // 10a: list all groups
  auto groups = reg.listGroupNames();
  std::cout << "  registered groups: " << groups.size() << "\n";
  std::cout << "  group list: ";
  for (const auto &g : groups)
    std::cout << g << " ";
  std::cout << "\n";

  bool hasFile = false, hasGit = false, hasShell = false;
  for (const auto &g : groups) {
    if (g == "file")
      hasFile = true;
    if (g == "git")
      hasGit = true;
    if (g == "shell")
      hasShell = true;
  }
  std::cout << "  file group: " << (hasFile ? "[OK]" : "[NO]") << "\n";
  std::cout << "  git group: " << (hasGit ? "[OK]" : "[NO]") << "\n";
  std::cout << "  shell group: " << (hasShell ? "[OK]" : "[NO]") << "\n";

  // 10b: query schemas by group
  auto gitSchemas = reg.listSchemas("git");
  std::cout << "\n  git group schema count: " << gitSchemas["tools"].size()
            << "\n";
  for (const auto &t : gitSchemas["tools"]) {
    std::cout << "    - " << t["function"]["name"] << "\n";
  }
  bool hasAllGitTools = gitSchemas["tools"].size() == 3;

  // 10c: query toolInfo by group
  auto fileInfo = reg.listToolInfo("file");
  std::cout << "\n  file group tool count: " << fileInfo["items"].size()
            << "\n";
  for (const auto &item : fileInfo["items"]) {
    std::cout << "    - " << item["name"] << " (risk: " << item["risk_level"]
              << ", group: " << item["group"] << ")\n";
  }
  bool hasAllFileTools = fileInfo["items"].size() == 6;

  // 10d: group prompts
  auto gitPrompt = reg.getGroupPrompt("git");
  std::cout << "\n  git group prompt: " << gitPrompt.substr(0, 80) << "...\n";
  auto filePrompt = reg.getGroupPrompt("file");
  std::cout << "  file group prompt: " << filePrompt.substr(0, 80) << "...\n";
  auto shellPrompt = reg.getGroupPrompt("shell");
  std::cout << "  shell group prompt: " << shellPrompt.substr(0, 80) << "...\n";
  auto unknownPrompt = reg.getGroupPrompt("web");
  std::cout << "  web group prompt (unregistered): \"" << unknownPrompt
            << "\"\n";

  bool hasPrompts =
      !gitPrompt.empty() && !filePrompt.empty() && !shellPrompt.empty();
  bool noUnknown = unknownPrompt.empty();

  bool testPassed = hasFile && hasGit && hasShell && hasAllGitTools &&
                    hasAllFileTools && hasPrompts && noUnknown;

  std::cout << "\n  >>> Test 10 " << (testPassed ? "PASSED" : "FAILED")
            << " <<<\n";
}

// ============================================================
// Main
// ============================================================
int main() {
  std::cout << "\n";
  std::cout
      << "+===========================================================+\n";
  std::cout
      << "|  CodePilot Tool System - Sprint 1+2+2.5 Complete Tests    |\n";
  std::cout
      << "|  New: git.commit / tool groups / group prompts            |\n";
  std::cout
      << "+===========================================================+\n";
  std::cout << "  Build: " << __DATE__ << " " << __TIME__ << "\n";
  std::cout << "  CWD: " << std::filesystem::current_path().string() << "\n";

  testToolSystemInit();
  testSchemaGeneration();
  testFileTools();
  testWorkspace();
  testRiskDetector();
  testPermissionManager();
  testEventBus();
  testRiskLevels();
  testCallWithPermission();
  testToolGroups();

  printSection("ALL TESTS COMPLETE");
  std::cout << "  Please verify each test PASSED/FAILED status.\n";
  std::cout << "  Sprint 2.5 additions:\n";
  std::cout << "    - git.commit tool (Dangerous, requires confirmation)\n";
  std::cout << "    - tool group system (group field + filtered queries)\n";
  std::cout
      << "    - group-level prompt snippets (for Agent System Prompt)\n\n";
  std::cout << "  Press any key to exit...\n";

  // prevent auto-exit for external environment review
  std::cin.get();

  return 0;
}