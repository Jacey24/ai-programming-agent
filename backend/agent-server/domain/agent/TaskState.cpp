#include <stdexcept>
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

    bool canTransition(TaskStatus target) const {
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
                return target == TaskStatus::Running     // 权限同意，继续执行
                    || target == TaskStatus::Failed      // 权限拒绝导致失败
                    || target == TaskStatus::Cancelled;

            case TaskStatus::Completed:
            case TaskStatus::Failed:
            case TaskStatus::Cancelled:
                return false;  // 终态不可再流转

            default:
                return false;
        }
    }

    void transition(TaskStatus target) {
        if (!canTransition(target)) {
            throw std::logic_error(
                std::string("Invalid state transition: ")
                + toString(status_) + " -> " + toString(target)
            );
        }
        status_ = target;
    }

    const char* toString(TaskStatus status) const {
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

private:
    TaskStatus status_ = TaskStatus::Created;
};

} // namespace codepilot