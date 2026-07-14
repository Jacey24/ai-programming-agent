#pragma once

namespace codepilot {

enum class ExecutionMode { Auto, DirectAnswer, WorkspaceAgent };

struct TaskRunOptions {
  ExecutionMode mode{ExecutionMode::Auto};
  bool autoRunSafeCommands{true};
  bool requireFileWritePermission{true};
  int maxSteps{6};
  int maxRoundsPerStep{3};
};

} // namespace codepilot
