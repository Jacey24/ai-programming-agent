#pragma once

#include <string>

namespace codepilot {

enum class TaskStatus {
    Created,
    Planning,
    Running,
    WaitingPermission,
    Completed,
    Failed,
    Cancelled
};

class TaskState {
public:
    TaskStatus current() const { return status_; }
    bool canTransition(TaskStatus target) const;
    void transition(TaskStatus target);
    const char* toString(TaskStatus status) const;

private:
    TaskStatus status_ = TaskStatus::Created;
};

} // namespace codepilot