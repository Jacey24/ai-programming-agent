#pragma once

#include "ExpertConfig.h"
#include "MessageBus.h"
#include "Plan.h"
#include <string>

namespace codepilot {

// ============================================================
// RouteEngine — 路由引擎
// 根据 Expert 的 nextRules 和 LLM 输出决定下一个 Expert
// ============================================================
class RouteEngine {
public:
  // 解析下一个 Expert 名称
  // 返回 Expert 名，或内置目标：
  //   "_done"            — AgentLoop 终止，正常结束
  //   "_user_interrupt"   — 等待用户输入（<ask> 触发）
  static std::string resolve(const ExpertConfig &expert,
                             const TagCollection &output,
                             const Plan &planStatus);

  // 快速解析（仅检查 <fail> 是否存在）
  static std::string resolveForFail(const ExpertConfig &expert);

private:
  // 检查条件是否匹配
  static bool matchCondition(const RouteRule &rule, const TagCollection &output,
                             const Plan &planStatus);
};

} // namespace codepilot