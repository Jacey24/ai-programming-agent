#include "TaskState.h"
#include <stdexcept>
#include <string>

namespace codepilot {

bool TaskState::canTransition(TaskStatus target) const {
    switch (status_) {
        case TaskStatus::Created:
            return target == TaskStatus::Planning || target == TaskStatus::Cancelled;

        case TaskStatus::Planning:
            return target == TaskStatus::Running || target == TaskStatus::Cancelled;

        case TaskStatus::Running:
            return target == TaskStatus::WaitingPermission
                || target == TaskStatus::Completed
                || target == TaskStatus::Failed
                || target == TaskStatus::Cancelled;

        case TaskStatus::WaitingPermission:
            return target == TaskStatus::Running
                || target == TaskStatus::Failed
                || target == TaskStatus::Cancelled;

        case TaskStatus::Completed:
        case TaskStatus::Failed:
        case TaskStatus::Cancelled:
            return false;

        default:
            return false;
    }
}

void TaskState::transition(TaskStatus target) {
    if (!canTransition(target)) {
        throw std::logic_error(
            std::string("Invalid state transition: ")
            + toString(status_) + " -> " + toString(target));
    }
    status_ = target;
}

const char* TaskState::toString(TaskStatus status) const {
    switch (status) {
        case TaskStatus::Created:           return "created";
        case TaskStatus::Planning:          return "planning";
        case TaskStatus::Running:           return "running";
        case TaskStatus::WaitingPermission: return "waiting_permission";
        case TaskStatus::Completed:         return "completed";
        case TaskStatus::Failed:            return "failed";
        case TaskStatus::Cancelled:         return "cancelled";
        default:                            return "unknown";
    }
}

} // namespace codepilot