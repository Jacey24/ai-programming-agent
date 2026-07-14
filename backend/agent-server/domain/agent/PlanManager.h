#pragma once

#include "MessageBus.h"
#include "Plan.h"
#include <mutex>
#include <string>
#include <vector>


namespace codepilot {

// ============================================================
// PlanManager — Plan 的运行时管理器
// 接收来自 MessageBus 的 <plan> 标签解析结果，操作 Plan 数据结构
// ============================================================
class PlanManager {
public:
  PlanManager() = default;

  // 获取只读快照
  Plan snapshot() const;

  // 从 <plan> 标签的解析结果中应用修改
  void applyPlanTags(const std::vector<ParsedTag> &planTags,
                     const std::string &changedBy);

  // 手动添加步骤
  void addStep(const std::string &description, int priority = 1);

  // 标记步骤完成
  void markDone(int index);

  // 标记步骤失败
  void markFailed(int index, const std::string &reason);

  // 插入新步骤到指定位置之后
  void insertAfter(int afterIndex, const std::string &description,
                   int priority = 1);

  // 查询
  bool isAllDone() const;
  bool hasFailed() const;
  int pendingCount() const;

  // 变更历史
  std::vector<PlanSnapshot> history() const;

private:
  Plan plan_;
  std::vector<PlanSnapshot> history_;
  mutable std::mutex mutex_;

  void recordSnapshot(const std::string &changedBy,
                      const std::string &description,
                      const std::string &summaryAtTime = "");
};

} // namespace codepilot