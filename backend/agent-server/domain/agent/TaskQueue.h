#pragma once

#include "TaskState.h"
#include <string>
#include <vector>

namespace codepilot {

struct PlanStep {
    std::string role;       // 角色名，如 "pm"、"coder"
    std::string action;     // 该步骤描述
    bool completed = false;
};

class TaskQueue {
public:
    void loadFromPlan(const std::vector<PlanStep>& steps);
    bool hasNext() const;
    PlanStep current() const;
    void markComplete();
    void markFailed();
    void insertAfterCurrent(const PlanStep& step);

    // 任务状态推导（替代硬编码 TaskState）
    TaskStatus deriveStatus() const;
    const char* deriveStatusString() const;  // 返回接口文档 8.3 的 status 字符串

    size_t size() const { return steps_.size(); }
    size_t completedCount() const;

private:
    std::vector<PlanStep> steps_;
    size_t currentIndex_ = 0;
    TaskState state_;

    size_t findFirstIncomplete() const;
};

} // namespace codepilot