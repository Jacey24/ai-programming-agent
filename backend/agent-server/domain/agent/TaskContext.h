#pragma once

#include "Plan.h"
#include <string>
#include <vector>

namespace codepilot {

// ============================================================
// TaskContext — Plan + Summary 绑定对象
// 传入每个 Expert 的 prompt，替换之前的独立 plan/summary/global
//
// v2 变更：
//   - sessionId → globalId（归属到 Global，不再是 Session）
//   - 移除 parentTaskId / loadedHistory / loadedPlanJson / loadedSummary
//     （跨 Task 上下文改为通过 Global 知识库检索）
// ============================================================
struct TaskContext {
  std::string taskId;
  std::string globalId;                  // 归属的 Global ID (g_xxx)
  std::string goal;                      // 用户原始需求
  Plan currentPlan;                      // 当前 plan 状态
  std::string summary;                   // 当前任务阶段的自然语言摘要
  std::vector<PlanSnapshot> planHistory; // plan 变更历史

  // 格式化为 prompt 片段（plan 状态 + 变更历史 + summary）
  std::string toPromptFragment() const {
    std::string out;

    out += currentPlan.toPromptFragment();
    out += "\n";

    // Summary
    out += "## 任务摘要\n";
    if (summary.empty()) {
      out += "（任务刚开始，暂无摘要）\n";
    } else {
      out += summary + "\n";
    }

    return out;
  }

  // 带变更历史的 prompt 片段
  std::string toPromptFragmentWithHistory() const {
    std::string out = toPromptFragment();

    if (!planHistory.empty()) {
      out += "\n## 计划变更历史\n";
      // 只展示最近 5 次变更
      size_t start = planHistory.size() > 5 ? planHistory.size() - 5 : 0;
      for (size_t i = start; i < planHistory.size(); ++i) {
        const auto &snap = planHistory[i];
        out += "  v" + std::to_string(snap.version) + " [" + snap.changedBy +
               "] " + snap.changeDescription;
        if (!snap.timestamp.empty()) {
          out += " (" + snap.timestamp + ")";
        }
        out += "\n";
      }
    }
    return out;
  }
};

} // namespace codepilot