#pragma once

#include <sqlite3.h>

#include <cstddef>

namespace codepilot {

struct TaskRecoveryReport {
  std::size_t tasksInterrupted{0};
  std::size_t permissionsExpired{0};
  std::size_t terminalEventsInserted{0};
};

// Reconciles task rows left in non-terminal execution states by a previous
// backend process. The operation is transactional and intentionally performs
// no in-memory task, LLM, tool, shell, or thread recovery.
TaskRecoveryReport recoverInterruptedTasks(sqlite3 *db);

} // namespace codepilot
