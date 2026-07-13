#include "PlanManager.h"
#include <chrono>
#include <ctime>
#include <sstream>

namespace codepilot {

namespace {
std::string currentTime() {
  auto now = std::chrono::system_clock::now();
  auto t = std::chrono::system_clock::to_time_t(now);
  std::tm *tm = std::gmtime(&t);
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", tm);
  return buf;
}
} // namespace

void PlanManager::recordSnapshot(const std::string &changedBy,
                                 const std::string &description,
                                 const std::string &summaryAtTime) {
  PlanSnapshot snap;
  snap.version = plan_.version;
  snap.planState = plan_;
  snap.summaryAtTime = summaryAtTime;
  snap.timestamp = currentTime();
  snap.changedBy = changedBy;
  snap.changeDescription = description;
  history_.push_back(snap);
}

Plan PlanManager::snapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return plan_;
}

void PlanManager::applyPlanTags(const std::vector<ParsedTag> &planTags,
                                const std::string &changedBy) {
  std::lock_guard<std::mutex> lock(mutex_);

  for (const auto &planTag : planTags) {
    for (const auto &child : planTag.children) {
      if (child.tagName == "add") {
        std::string desc = child.content;
        int priority = child.attributes.value("priority", 1);
        addStep(desc, priority);
      } else if (child.tagName == "complete") {
        int idx = child.attributes.value("index", -1);
        if (idx >= 0 && idx < static_cast<int>(plan_.steps.size())) {
          plan_.steps[idx].state = Plan::Step::Done;
          plan_.version++;
        }
      } else if (child.tagName == "fail") {
        int idx = child.attributes.value("index", -1);
        if (idx >= 0 && idx < static_cast<int>(plan_.steps.size())) {
          plan_.steps[idx].state = Plan::Step::Failed;
          plan_.steps[idx].failureReason =
              child.attributes.value("reason", "未指定原因");
          plan_.version++;
        }
      }
      // <status/> 是只读请求，在 MessageBus 层处理，不需要修改 plan
    }
  }

  recordSnapshot(changedBy, "应用 plan 标签修改");
}

void PlanManager::addStep(const std::string &description, int priority) {
  Plan::Step step;
  step.index = static_cast<int>(plan_.steps.size());
  step.description = description;
  step.priority = priority;
  plan_.steps.push_back(step);
  plan_.version++;

  std::ostringstream desc;
  desc << "新增步骤: " << description;
  // recordSnapshot 已在 applyPlanTags 中调用，这里仅修改数据
}

void PlanManager::markDone(int index) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (index >= 0 && index < static_cast<int>(plan_.steps.size())) {
    plan_.steps[index].state = Plan::Step::Done;
    plan_.version++;

    std::ostringstream desc;
    desc << "标记步骤 " << index << " 完成: " << plan_.steps[index].description;
    recordSnapshot("system", desc.str());
  }
}

void PlanManager::markFailed(int index, const std::string &reason) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (index >= 0 && index < static_cast<int>(plan_.steps.size())) {
    plan_.steps[index].state = Plan::Step::Failed;
    plan_.steps[index].failureReason = reason;
    plan_.version++;

    std::ostringstream desc;
    desc << "标记步骤 " << index << " 失败: " << reason;
    recordSnapshot("system", desc.str());
  }
}

void PlanManager::insertAfter(int afterIndex, const std::string &description,
                              int priority) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (afterIndex < 0 || afterIndex >= static_cast<int>(plan_.steps.size()))
    return;

  Plan::Step step;
  step.description = description;
  step.priority = priority;

  auto it = plan_.steps.begin() + afterIndex + 1;
  plan_.steps.insert(it, step);

  // 重新编号
  for (size_t i = 0; i < plan_.steps.size(); ++i) {
    plan_.steps[i].index = static_cast<int>(i);
  }
  plan_.version++;

  std::ostringstream desc;
  desc << "在步骤 " << afterIndex << " 后插入: " << description;
  recordSnapshot("system", desc.str());
}

bool PlanManager::isAllDone() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return plan_.isAllDone();
}

bool PlanManager::hasFailed() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return plan_.hasFailed();
}

int PlanManager::pendingCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return plan_.pendingCount();
}

std::vector<PlanSnapshot> PlanManager::history() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return history_;
}

} // namespace codepilot