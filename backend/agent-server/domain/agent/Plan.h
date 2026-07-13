#pragma once

#include <string>
#include <vector>

namespace codepilot {

// ============================================================
// Plan — 动态计划数据结构
// 所有 Expert 通过 <plan> 标签操作，AgentLoop 解析后调用 Plan
// ============================================================
struct Plan {
  struct Step {
    int index = 0;
    std::string description;
    int priority = 1; // 1-10，1 最高

    enum State {
      Pending,
      InProgress,
      Done,
      Failed,
    };
    State state = Pending;

    std::string assignedTo;    // 可选：指定 Expert
    std::string failureReason; // 失败原因
  };

  std::vector<Step> steps;
  int version = 0; // 每次修改自增

  // 查询
  bool isAllDone() const {
    if (steps.empty())
      return false;
    for (const auto &s : steps) {
      if (s.state != Step::Done)
        return false;
    }
    return true;
  }

  bool hasFailed() const {
    for (const auto &s : steps) {
      if (s.state == Step::Failed)
        return true;
    }
    return false;
  }

  int pendingCount() const {
    int count = 0;
    for (const auto &s : steps) {
      if (s.state == Step::Pending || s.state == Step::InProgress)
        ++count;
    }
    return count;
  }

  // 格式化为 prompt 片段
  std::string toPromptFragment() const {
    if (steps.empty())
      return "（暂无计划步骤）";

    std::string out = "## 当前计划 (v" + std::to_string(version) + ")\n";
    for (const auto &s : steps) {
      out += "  [";
      switch (s.state) {
      case Step::Done:
        out += "✓";
        break;
      case Step::InProgress:
        out += "▶";
        break;
      case Step::Failed:
        out += "✗";
        break;
      default:
        out += " ";
        break;
      }
      out += "] " + std::to_string(s.index) + ". " + s.description;
      if (s.state == Step::Failed && !s.failureReason.empty()) {
        out += " (失败: " + s.failureReason + ")";
      }
      if (!s.assignedTo.empty()) {
        out += " [" + s.assignedTo + "]";
      }
      out += "\n";
    }
    return out;
  }
};

// ============================================================
// PlanSnapshot — Plan 变更历史快照
// ============================================================
struct PlanSnapshot {
  int version = 0;
  Plan planState;            // 当时的 plan 状态
  std::string summaryAtTime; // 当时的 summary
  std::string timestamp;
  std::string changedBy;         // 哪个 Expert 触发的变更
  std::string changeDescription; // 变更描述
};

} // namespace codepilot