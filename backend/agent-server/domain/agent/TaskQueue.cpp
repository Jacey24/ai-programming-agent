#include "TaskQueue.h"

namespace codepilot {

void TaskQueue::loadFromPlan(const std::vector<PlanStep>& steps) {
    steps_ = steps;
    currentIndex_ = 0;
    state_.transition(TaskStatus::Planning);
}

bool TaskQueue::hasNext() const {
    return findFirstIncomplete() < steps_.size();
}

PlanStep TaskQueue::current() const {
    size_t idx = findFirstIncomplete();
    if (idx < steps_.size()) {
        return steps_[idx];
    }
    return {};
}

void TaskQueue::markComplete() {
    size_t idx = findFirstIncomplete();
    if (idx < steps_.size()) {
        steps_[idx].completed = true;
        currentIndex_ = idx + 1;
    }
    if (!hasNext()) {
        state_.transition(TaskStatus::Completed);
    }
}

void TaskQueue::insertAfterCurrent(const PlanStep& step) {
    size_t idx = findFirstIncomplete();
    if (idx < steps_.size()) {
        steps_.insert(steps_.begin() + static_cast<long>(idx) + 1, step);
    } else {
        steps_.push_back(step);
    }
}

size_t TaskQueue::completedCount() const {
    size_t count = 0;
    for (const auto& s : steps_) {
        if (s.completed) ++count;
    }
    return count;
}

size_t TaskQueue::findFirstIncomplete() const {
    for (size_t i = currentIndex_; i < steps_.size(); ++i) {
        if (!steps_[i].completed) {
            return i;
        }
    }
    return steps_.size();
}

TaskStatus TaskQueue::deriveStatus() const {
    if (hasNext()) {
        return TaskStatus::Running;
    }
    return TaskStatus::Completed;
}

const char* TaskQueue::deriveStatusString() const {
    return state_.toString(deriveStatus());
}

} // namespace codepilot