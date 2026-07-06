#pragma once

namespace codepilot {

enum class EventType {
    TaskCreated,
    TaskPlanning,
    AgentMessage,
    ToolStarted,
    ToolOutput,
    ToolFinished,
    PermissionRequired,
    PermissionResolved,
    FileChanged,
    TaskCompleted,
    TaskFailed,
    TaskCancelled
};

} // namespace codepilot
