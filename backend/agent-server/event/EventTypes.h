#pragma once

namespace codepilot {

enum class EventType {
    TaskCreated,
    TaskPlanning,
    AgentMessage,
    AgentMessageChunk,
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
