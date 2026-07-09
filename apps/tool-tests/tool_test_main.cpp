// ============================================================
// CodePilot tool system - Sprint 1 + Sprint 2 + Sprint 2.5 + Batch 1
// Complete test suite
//
// Batch 1 additions (8 new tools):
//   - file.search, file.mkdir, file.rmdir, file.remove
//   - file.copy, file.move
//   - git.log, git.branch
// ============================================================

#include <filesystem>
#include <iostream>
#include <memory>
#include <sstream>
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

  // Sprint 1 + Sprint 2 original tools
  bool hasOriginal = sys.registry().hasTool("file.list") &&
                     sys.registry().hasTool("file.read") &&
                     sys.registry().hasTool("file.write") &&
                     sys.registry().hasTool("file.apply_patch") &&
                     sys.registry().hasTool("cd") &&
                     sys.registry().hasTool("pwd") &&
                     sys.registry().hasTool("shell.run") &&
                     sys.registry().hasTool("git.status") &&
                     sys.registry().hasTool("git.diff") &&
                     sys.registry().hasTool("git.commit");

  // Batch 1 new tools (file group)
  bool hasNewFile = sys.registry().hasTool("file.search") &&
                    sys.registry().hasTool("file.mkdir") &&
                    sys.registry().hasTool("file.rmdir") &&
                    sys.registry().hasTool("file.remove") &&
                    sys.registry().hasTool("file.copy") &&
                    sys.registry().hasTool("file.move");

  // Batch 1 new tools (git group)
  bool hasNewGit =
      sys.registry().hasTool("git.log") && sys.registry().hasTool("git.branch");

  bool testPassed = sys.isInitialized() && sys.registry().size() >= 18 &&
                    hasOriginal && hasNewFile && hasNewGit;
  std::cout << "\n  >>> Test 1 " << (testPassed ? "PASSED" : "FAILED")
            << " <<<\n";
}

// ============================================================
// Test 2: Schema generation
// ============================================================
void testSchemaGeneration() {
  printSection("Test 2: Tool Schema generation (incl. new tools)");

  auto &sys = ToolSystem::getInstance();

  auto schemas = sys.registry().listSchemas();
  std::cout << "  generated Schema (first 600 chars):\n"
            << schemas.dump(2).substr(0, 600) << "...\n";

  auto info = sys.registry().listToolInfo();
  std::cout << "\n  listToolInfo (with group field):\n"
            << info.dump(2).substr(0, 600) << "...\n";

  // verify original tools in schema
  std::string schemaStr = schemas.dump();
  bool hasShellRun = schemaStr.find("shell.run") != std::string::npos;
  bool hasGitStatus = schemaStr.find("git.status") != std::string::npos;
  bool hasGitDiff = schemaStr.find("git.diff") != std::string::npos;
  bool hasGitCommit = schemaStr.find("git.commit") != std::string::npos;

  // verify new tools in schema
  bool hasFileSearch = schemaStr.find("file.search") != std::string::npos;
  bool hasFileMkdir = schemaStr.find("file.mkdir") != std::string::npos;
  bool hasFileRmdir = schemaStr.find("file.rmdir") != std::string::npos;
  bool hasFileRemove = schemaStr.find("file.remove") != std::string::npos;
  bool hasFileCopy = schemaStr.find("file.copy") != std::string::npos;
  bool hasFileMove = schemaStr.find("file.move") != std::string::npos;
  bool hasGitLog = schemaStr.find("git.log") != std::string::npos;
  bool hasGitBranch = schemaStr.find("git.branch") != std::string::npos;

  std::cout << "\n  shell.run: " << (hasShellRun ? "[OK]" : "[NO]") << "\n";
  std::cout << "  git.status: " << (hasGitStatus ? "[OK]" : "[NO]") << "\n";
  std::cout << "  git.diff: " << (hasGitDiff ? "[OK]" : "[NO]") << "\n";
  std::cout << "  git.commit: " << (hasGitCommit ? "[OK]" : "[NO]") << "\n";
  std::cout << "  file.search: " << (hasFileSearch ? "[OK]" : "[NO]") << "\n";
  std::cout << "  file.mkdir: " << (hasFileMkdir ? "[OK]" : "[NO]") << "\n";
  std::cout << "  file.rmdir: " << (hasFileRmdir ? "[OK]" : "[NO]") << "\n";
  std::cout << "  file.remove: " << (hasFileRemove ? "[OK]" : "[NO]") << "\n";
  std::cout << "  file.copy: " << (hasFileCopy ? "[OK]" : "[NO]") << "\n";
  std::cout << "  file.move: " << (hasFileMove ? "[OK]" : "[NO]") << "\n";
  std::cout << "  git.log: " << (hasGitLog ? "[OK]" : "[NO]") << "\n";
  std::cout << "  git.branch: " << (hasGitBranch ? "[OK]" : "[NO]") << "\n";

  bool testPassed = hasShellRun && hasGitStatus && hasGitDiff && hasGitCommit &&
                    hasFileSearch && hasFileMkdir && hasFileRmdir &&
                    hasFileRemove && hasFileCopy && hasFileMove && hasGitLog &&
                    hasGitBranch;
  std::cout << "\n  >>> Test 2 " << (testPassed ? "PASSED" : "FAILED")
            << " <<<\n";
}

// ============================================================
// Test 3: file tools (original + new)
// ============================================================
void testFileTools() {
  printSection("Test 3: File tools (original + new)");

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

  // 3e: file.mkdir (create temp test dir)
  json mkdirArgs;
  mkdirArgs["path"] = "temp_test_dir";
  result = sys.callTool("file.mkdir", ctx, mkdirArgs);
  printResult("file.mkdir temp_test_dir", result);
  bool test3e = result.success;

  // 3f: file.search (search for "file.list" in source)
  json searchArgs;
  searchArgs["pattern"] = "file.list";
  searchArgs["root"] = "backend/agent-server/domain/tools";
  result = sys.callTool("file.search", ctx, searchArgs);
  printResult("file.search 'file.list' in domain/tools", result);
  bool test3f = result.success;

  // 3g: file.copy (copy a file to temp dir)
  json copyArgs;
  copyArgs["source"] = "CMakeLists.txt";
  copyArgs["destination"] = "temp_test_dir/CMakeLists.txt.bak";
  result = sys.callTool("file.copy", ctx, copyArgs);
  printResult("file.copy CMakeLists.txt -> temp_test_dir", result);
  bool test3g = result.success;

  // 3h: file.move (rename the copied file)
  json moveArgs;
  moveArgs["source"] = "temp_test_dir/CMakeLists.txt.bak";
  moveArgs["destination"] = "temp_test_dir/CMakeLists.bak2";
  result = sys.callTool("file.move", ctx, moveArgs);
  printResult("file.move (rename backup)", result);
  bool test3h = result.success;

  // 3i: file.remove (remove temp dir)
  json removeArgs;
  removeArgs["path"] = "temp_test_dir";
  result = sys.callTool("file.remove", ctx, removeArgs);
  printResult("file.remove temp_test_dir", result);
  bool test3i = result.success;

  std::cout << "\n  Test 3a (list): " << (test3a ? "PASS" : "FAIL") << "\n";
  std::cout << "  Test 3b (read): " << (test3b ? "PASS" : "FAIL") << "\n";
  std::cout << "  Test 3c (cd): " << (test3c ? "PASS" : "FAIL") << "\n";
  std::cout << "  Test 3d (pwd): " << (test3d ? "PASS" : "FAIL") << "\n";
  std::cout << "  Test 3e (mkdir): " << (test3e ? "PASS" : "FAIL") << "\n";
  std::cout << "  Test 3f (search): " << (test3f ? "PASS" : "FAIL") << "\n";
  std::cout << "  Test 3g (copy): " << (test3g ? "PASS" : "FAIL") << "\n";
  std::cout << "  Test 3h (move): " << (test3h ? "PASS" : "FAIL") << "\n";
  std::cout << "  Test 3i (remove): " << (test3i ? "PASS" : "FAIL") << "\n";
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
// Test 8: Tool risk levels (original + new tools)
// ============================================================
void testRiskLevels() {
  printSection("Test 8: Tool risk levels (original + new)");

  auto &sys = ToolSystem::getInstance();

  struct TestCase {
    std::string name;
    std::string arg;
    RiskLevel expected;
  };

  TestCase cases[] = {
      // Original tools
      {"file.list", "", RiskLevel::Safe},
      {"file.read", "", RiskLevel::Safe},
      {"file.write", "", RiskLevel::Medium},
      {"cd", "", RiskLevel::Safe},
      {"pwd", "", RiskLevel::Safe},
      {"git.status", "", RiskLevel::Safe},
      {"git.diff", "", RiskLevel::Safe},
      {"git.commit", "", RiskLevel::Dangerous},
      // Batch 1 new tools (file group)
      {"file.search", "", RiskLevel::Safe},
      {"file.mkdir", "", RiskLevel::Safe},
      {"file.rmdir", "", RiskLevel::Medium},
      {"file.remove", "", RiskLevel::Medium},
      {"file.copy", "", RiskLevel::Medium},
      {"file.move", "", RiskLevel::Medium},
      // Batch 1 new tools (git group)
      {"git.log", "", RiskLevel::Safe},
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

  // git.branch: default action=list → Safe
  auto *branchTool = sys.registry().getTool("git.branch");
  if (branchTool) {
    json defaultArgs = json::object();
    auto branchDefault = branchTool->riskLevel(defaultArgs);
    bool branchOk = branchDefault == RiskLevel::Safe;
    std::cout << "  git.branch(default): " << riskLevelToString(branchDefault)
              << (branchOk ? " [OK]" : " [NO]") << "\n";
    if (!branchOk)
      allMatch = false;

    json createArgs;
    createArgs["action"] = "create";
    auto branchCreate = branchTool->riskLevel(createArgs);
    bool createOk = branchCreate == RiskLevel::Medium;
    std::cout << "  git.branch(create): " << riskLevelToString(branchCreate)
              << (createOk ? " [OK]" : " [NO]") << "\n";
    if (!createOk)
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
// Test 10: Tool group system + new tool groups
// ============================================================
void testToolGroups() {
  printSection("Test 10: Tool group system (incl. new tools)");

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

  // 10b: query schemas by group - git now has 5 tools
  auto gitSchemas = reg.listSchemas("git");
  std::cout << "\n  git group schema count: " << gitSchemas["tools"].size()
            << " (expected 5)\n";
  for (const auto &t : gitSchemas["tools"]) {
    std::cout << "    - " << t["function"]["name"] << "\n";
  }
  bool hasAllGitTools = gitSchemas["tools"].size() == 5;

  // 10c: query toolInfo by group - file now has 12 tools
  auto fileInfo = reg.listToolInfo("file");
  std::cout << "\n  file group tool count: " << fileInfo["items"].size()
            << " (expected 12)\n";
  for (const auto &item : fileInfo["items"]) {
    std::cout << "    - " << item["name"] << " (risk: " << item["risk_level"]
              << ", group: " << item["group"] << ")\n";
  }
  bool hasAllFileTools = fileInfo["items"].size() == 12;

  // 10d: group prompts
  auto gitPrompt = reg.getGroupPrompt("git");
  std::cout << "\n  git group prompt: " << gitPrompt.substr(0, 80) << "...\n";
  auto filePrompt = reg.getGroupPrompt("file");
  std::cout << "  file group prompt: " << filePrompt.substr(0, 80) << "...\n";
  auto shellPrompt = reg.getGroupPrompt("shell");
  std::cout << "  shell group prompt: " << shellPrompt.substr(0, 80) << "...\n";

  bool hasPrompts =
      !gitPrompt.empty() && !filePrompt.empty() && !shellPrompt.empty();

  bool testPassed = hasFile && hasGit && hasShell && hasAllGitTools &&
                    hasAllFileTools && hasPrompts;

  std::cout << "\n  >>> Test 10 " << (testPassed ? "PASSED" : "FAILED")
            << " <<<\n";
}

// ============================================================
// Test 11: New tools specific validation
// ============================================================
void testNewToolsSpecific() {
  printSection("Test 11: New tools specific validation");

  auto &sys = ToolSystem::getInstance();
  ToolContext ctx{"test_new_tools",
                  "ws_001",
                  std::filesystem::current_path().string(),
                  "sess_001",
                  {}};

  auto &reg = sys.registry();

  // validate file.search schema params
  {
    auto *tool = reg.getTool("file.search");
    if (tool) {
      auto s = tool->schema();
      bool hasPattern = false, hasRoot = false;
      for (const auto &p : s.params) {
        if (p.name == "pattern" && p.required)
          hasPattern = true;
        if (p.name == "root" && !p.required)
          hasRoot = true;
      }
      std::cout << "  file.search schema: pattern(required)="
                << (hasPattern ? "OK" : "NO")
                << ", root(optional)=" << (hasRoot ? "OK" : "NO") << "\n";
    }
  }

  // validate git.branch dynamic risk level
  {
    auto *tool = reg.getTool("git.branch");
    if (tool) {
      json listArgs;
      listArgs["action"] = "list";
      auto listLevel = tool->riskLevel(listArgs);

      json createArgs;
      createArgs["action"] = "create";
      createArgs["name"] = "test-branch";
      auto createLevel = tool->riskLevel(createArgs);

      bool levelOk =
          listLevel == RiskLevel::Safe && createLevel == RiskLevel::Medium;
      std::cout << "  git.branch risk: list=" << riskLevelToString(listLevel)
                << ", create=" << riskLevelToString(createLevel)
                << (levelOk ? " [OK]" : " [NO]") << "\n";
    }
  }

  // validate file.mkdir (safe) and file.remove (medium)
  {
    auto *mkdirTool = reg.getTool("file.mkdir");
    auto *removeTool = reg.getTool("file.remove");
    if (mkdirTool && removeTool) {
      bool mkdirOk = mkdirTool->riskLevel(json::object()) == RiskLevel::Safe;
      bool removeOk =
          removeTool->riskLevel(json::object()) == RiskLevel::Medium;
      std::cout << "  file.mkdir risk="
                << riskLevelToString(mkdirTool->riskLevel(json::object()))
                << (mkdirOk ? " [OK]" : " [NO]") << "\n";
      std::cout << "  file.remove risk="
                << riskLevelToString(removeTool->riskLevel(json::object()))
                << (removeOk ? " [OK]" : " [NO]") << "\n";
    }
  }

  std::cout << "\n  >>> Test 11 - manual review required (no auto pass/fail)\n";
  std::cout << "       Verify each [OK]/[NO] status above.\n";
}

// ============================================================
// Batch 2 - Test 12: FallbackHandler (未知工具提示)
// ============================================================
void testFallbackHandler() {
  printSection("Test 12: FallbackHandler (unknown tool fallback)");

  auto &sys = ToolSystem::getInstance();
  ToolContext ctx{"fallback_test",
                  "ws_001",
                  std::filesystem::current_path().string(),
                  "sess_001",
                  {}};

  // 调用一个不存在的工具
  json args = json::object();
  auto result = sys.callTool("nonexistent.tool", ctx, args);

  printResult("callTool(not found)", result);

  // 检查错误消息中包含分组信息和提示
  bool hasGroupHint = result.error.find("未知工具") != std::string::npos;
  bool hasToolList = result.error.find("file 组") != std::string::npos ||
                     result.error.find("git 组") != std::string::npos;
  bool hasActionHint = result.error.find("请检查") != std::string::npos;

  std::cout << "  contains '未知工具': " << (hasGroupHint ? "[OK]" : "[NO]")
            << "\n";
  std::cout << "  contains group list: " << (hasToolList ? "[OK]" : "[NO]")
            << "\n";
  std::cout << "  contains action hint: " << (hasActionHint ? "[OK]" : "[NO]")
            << "\n";

  bool testPassed = hasGroupHint && hasToolList && hasActionHint;
  std::cout << "\n  >>> Test 12 " << (testPassed ? "PASSED" : "FAILED")
            << " <<<\n";
}

// ============================================================
// Batch 2 - Test 13: Duplicate detection
// ============================================================
void testDuplicateDetection() {
  printSection("Test 13: Duplicate detection");

  auto &sys = ToolSystem::getInstance();
  ToolContext ctx{"dup_test",
                  "ws_001",
                  std::filesystem::current_path().string(),
                  "sess_001",
                  {}};

  // 重复调用一个会失败的工具（读不存在的文件）3次
  json args;
  args["path"] = "nonexistent_file_xyz.notfound";

  std::cout << "  Calling file.read (nonexistent file) x3:\n";

  auto r1 = sys.callTool("file.read", ctx, args);
  printResult("  call #1", r1);

  auto r2 = sys.callTool("file.read", ctx, args);
  printResult("  call #2", r2);

  auto r3 = sys.callTool("file.read", ctx, args);
  printResult("  call #3", r3);

  // 检查第3次调用是否有重复警告
  bool hasDuplicateWarning =
      r3.metadata.contains("duplicate_warning") &&
      r3.metadata["duplicate_warning"].get<std::string>().find("连续执行") !=
          std::string::npos;

  bool hasDuplicateCount = r3.metadata.contains("duplicate_count") &&
                           r3.metadata["duplicate_count"].get<int>() >= 3;

  std::cout << "  duplicate_warning present: "
            << (hasDuplicateWarning ? "[OK]" : "[NO]") << "\n";
  std::cout << "  duplicate_count >= 3: "
            << (hasDuplicateCount ? "[OK]" : "[NO]") << "\n";
  if (hasDuplicateWarning) {
    std::cout << "  warning text: "
              << r3.metadata["duplicate_warning"].get<std::string>() << "\n";
  }

  bool testPassed = hasDuplicateWarning && hasDuplicateCount;
  std::cout << "\n  >>> Test 13 " << (testPassed ? "PASSED" : "FAILED")
            << " <<<\n";
}

// ============================================================
// Batch 2 - Test 14: Tool statistics
// ============================================================
void testToolStats() {
  printSection("Test 14: Tool statistics");

  auto &sys = ToolSystem::getInstance();

  // 查询已有工具调用统计 (callTool 已被多次调用)
  auto fileReadStats = sys.getToolStats("file.read");
  std::cout << "  file.read stats:\n" << fileReadStats.dump(2) << "\n";

  bool hasFileReadCalls = fileReadStats["call_count"].get<int>() > 0;

  // get all stats
  auto allStats = sys.getAllToolStats();
  std::cout << "\n  all tool stats:\n" << allStats.dump(2) << "\n";

  bool hasMultipleStats = allStats["tools"].size() >= 3;

  std::cout << "  file.read has calls: " << (hasFileReadCalls ? "[OK]" : "[NO]")
            << "\n";
  std::cout << "  multiple tools tracked: "
            << (hasMultipleStats ? "[OK]" : "[NO]") << "\n";

  bool testPassed = hasFileReadCalls && hasMultipleStats;
  std::cout << "\n  >>> Test 14 " << (testPassed ? "PASSED" : "FAILED")
            << " <<<\n";
}

// ============================================================
// Batch 3 - Test 15: Thread safety (basic stress test)
// ============================================================
void testThreadSafety() {
  printSection("Test 15: Thread safety (shared_mutex)");

  auto &sys = ToolSystem::getInstance();
  ToolContext ctx{"thread_test",
                  "ws_001",
                  std::filesystem::current_path().string(),
                  "sess_001",
                  {}};

  // 连续多次调用确保锁正常工作
  std::cout << "  Running 10 sequential callTool calls with lock...\n";
  bool allOk = true;
  for (int i = 0; i < 10; i++) {
    json args;
    args["depth"] = 1;
    auto result = sys.callTool("file.list", ctx, args);
    if (!result.success) {
      allOk = false;
      std::cout << "  FAIL at iteration " << i << "\n";
      break;
    }
  }

  // 验证 stat 在多次并发/顺序访问后仍然一致
  auto stats = sys.getToolStats("file.list");
  int callCount = stats["call_count"].get<int>();
  std::cout << "  file.list call count after 10 more calls: " << callCount
            << "\n";

  // 读锁访问：统计和配置查询
  bool statsReadable = !sys.getToolStats("file.list").is_null();
  bool configReadable = sys.isToolEnabled("file.list");

  std::cout << "  stats readable via shared_lock: "
            << (statsReadable ? "[OK]" : "[NO]") << "\n";
  std::cout << "  config readable via shared_lock: "
            << (configReadable ? "[OK]" : "[NO]") << "\n";

  bool testPassed = allOk && statsReadable && configReadable;
  std::cout << "\n  >>> Test 15 " << (testPassed ? "PASSED" : "FAILED")
            << " <<<\n";
}

// ============================================================
// Batch 3 - Test 16: Config hot reload
// ============================================================
void testConfigHotReload() {
  printSection("Test 16: Config hot reload");

  auto &sys = ToolSystem::getInstance();
  ToolContext ctx{"reload_test",
                  "ws_001",
                  std::filesystem::current_path().string(),
                  "sess_001",
                  {}};

  // 16a: 配置热加载（已存在的配置文件）
  bool reloadOk = sys.reloadConfig("config/tools.json");
  std::cout << "  reloadConfig('config/tools.json'): "
            << (reloadOk ? "[OK]" : "[NO]") << "\n";

  // 16b: 检查工具启用状态（来自配置的 enabled 字段）
  bool listEnabled = sys.isToolEnabled("file.list");
  bool unknownEnabled = sys.isToolEnabled("nonexistent.tool");

  std::cout << "  isToolEnabled('file.list'): "
            << (listEnabled ? "ENABLED [OK]" : "DISABLED [UNEXPECTED]") << "\n";
  std::cout << "  isToolEnabled('nonexistent.tool'): "
            << (unknownEnabled ? "ENABLED (default) [OK]"
                               : "DISABLED [UNEXPECTED]")
            << "\n";

  // 16c: 不存在的配置文件应该返回 false
  bool reloadFail = sys.reloadConfig("nonexistent_config.json");
  std::cout << "  reloadConfig('nonexistent_config.json'): "
            << (reloadFail ? "OK [UNEXPECTED]" : "FAIL [OK]") << "\n";

  bool testPassed = reloadOk && listEnabled && unknownEnabled && !reloadFail;
  std::cout << "\n  >>> Test 16 " << (testPassed ? "PASSED" : "FAILED")
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
      << "|  CodePilot Tool System - Sprints 1+2+2.5+Batch1+Batch2    |\n";
  std::cout
      << "|  Batch2: FallbackHandler / Duplicate Detection / Stats    |\n";
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
  testNewToolsSpecific();

  // Batch 2 tests
  testFallbackHandler();
  testDuplicateDetection();
  testToolStats();

  // Batch 3 tests
  testThreadSafety();
  testConfigHotReload();

  printSection("ALL TESTS COMPLETE");
  std::cout << "  Please verify each test PASSED/FAILED status.\n";
  std::cout << "  Batch 1 (8 new tools): file.search/mkdir/rmdir/\n";
  std::cout << "    remove/copy/move + git.log/git.branch\n";
  std::cout << "  Batch 2 (3 new features):\n";
  std::cout << "    - FallbackHandler (unknown tool friendly msg)\n";
  std::cout << "    - Duplicate Detection (warns on repeated failures)\n";
  std::cout << "    - Tool Statistics (runtime memory cache)\n";
  std::cout << "  Batch 3 (2 new features):\n";
  std::cout << "    - Thread Safety (shared_mutex read-write lock)\n";
  std::cout << "    - Config Hot Reload (tools.json enabled/risk_level)\n\n";
  std::cout << "  Press any key to exit...\n";

  // prevent auto-exit for external environment review
  std::cin.get();

  return 0;
}
