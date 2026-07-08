#pragma once

#include "domain/security/RiskDetector.h"
#include "domain/tools/BuiltinShell.h"
#include "domain/tools/FileTool.h"
#include "domain/tools/Tool.h"
#include "domain/tools/ToolRegistry.h"
#include "event/EventBus.h"
#include "infrastructure/filesystem/Workspace.h"
#include "infrastructure/process/ProcessRunner.h"


#include <memory>
#include <string>

namespace codepilot {

class ToolSystem {
public:
  static ToolSystem &getInstance();

  void init(const std::string &workspacePath,
            const std::string &configPath = "");

  ToolRegistry &registry();
  EventBus &eventBus();
  Workspace &workspace();

  ToolResult callTool(const std::string &name, const ToolContext &context,
                      const json &arguments);

  bool isInitialized() const { return initialized_; }

private:
  ToolSystem() = default;
  ~ToolSystem() = default;
  ToolSystem(const ToolSystem &) = delete;
  ToolSystem &operator=(const ToolSystem &) = delete;

  std::shared_ptr<Workspace> workspace_;
  std::shared_ptr<BuiltinShell> shell_;
  std::unique_ptr<ToolRegistry> registry_;
  std::shared_ptr<EventBus> eventBus_;
  std::shared_ptr<ProcessRunner> runner_;
  std::shared_ptr<RiskDetector> detector_;
  bool initialized_{false};
};

} // namespace codepilot