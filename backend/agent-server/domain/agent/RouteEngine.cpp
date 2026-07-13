#include "RouteEngine.h"
#include <algorithm>

namespace codepilot {

bool RouteEngine::matchCondition(const RouteRule &rule,
                                 const TagCollection &output,
                                 const Plan &planStatus) {
  switch (rule.conditionType) {
  case RouteRule::TagExists:
    return output.has(rule.conditionValue);

  case RouteRule::TagValueMatch: {
    // 检查标签内容是否包含指定值
    auto tags = output.get(rule.conditionValue);
    for (const auto &tag : tags) {
      if (tag.content.find(rule.conditionValue) != std::string::npos ||
          (rule.conditionValue == "done" && output.has("done"))) {
        return true;
      }
    }
    return output.has(rule.conditionValue);
  }

  case RouteRule::PlanState:
    if (rule.conditionValue == "all_done")
      return planStatus.isAllDone();
    if (rule.conditionValue == "has_failed")
      return planStatus.hasFailed();
    if (rule.conditionValue == "has_pending")
      return planStatus.pendingCount() > 0;
    if (rule.conditionValue == "is_empty")
      return planStatus.steps.empty();
    return false;

  case RouteRule::Default:
    return true; // 始终匹配

  default:
    return false;
  }
}

std::string RouteEngine::resolve(const ExpertConfig &expert,
                                 const TagCollection &output,
                                 const Plan &planStatus) {
  // 特殊处理的标签（优先级最高，不通过 nextRules 路由）

  // 1. <ask> — 用户交互
  if (output.has("ask")) {
    return "_user_interrupt";
  }

  // 2. <done> — 走 nextRules
  // 3. <fail> — 走 onFailRoute
  if (output.has("fail")) {
    return resolveForFail(expert);
  }

  // 遍历 nextRules，找所有匹配的规则
  const RouteRule *bestRule = nullptr;
  int bestPriority = -1;

  for (const auto &rule : expert.nextRules) {
    if (matchCondition(rule, output, planStatus)) {
      if (rule.priority > bestPriority) {
        bestPriority = rule.priority;
        bestRule = &rule;
      }
    }
  }

  if (bestRule) {
    return bestRule->routeTo;
  }

  // 无匹配规则：如果有 <done> 标签，默认 _done
  if (output.has("done")) {
    return "_done";
  }

  // 完全无匹配：返回空字符串（AgentLoop 应终止）
  return "";
}

std::string RouteEngine::resolveForFail(const ExpertConfig &expert) {
  if (!expert.onFailRoute.empty()) {
    return expert.onFailRoute;
  }
  // 未配置 onFailRoute，默认终止
  return "_done";
}

} // namespace codepilot