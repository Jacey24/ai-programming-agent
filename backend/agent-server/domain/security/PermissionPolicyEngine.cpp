#include "domain/security/PermissionPolicyEngine.h"

#include <nlohmann/json.hpp>

namespace codepilot {

PolicyAction
PermissionPolicyEngine::evaluate(const std::string &permissions_config,
                                 const std::string &toolName) {
  // 默认策略：走标准权限请求流程
  if (permissions_config.empty() || permissions_config == "{}") {
    return PolicyAction::Ask;
  }

  try {
    auto config = nlohmann::json::parse(permissions_config);
    if (!config.is_object()) {
      return PolicyAction::Ask;
    }

    // 1. 先精确匹配 toolName
    if (config.contains(toolName)) {
      const auto &value = config[toolName];
      if (value.is_string()) {
        const std::string action = value.get<std::string>();
        if (action == "auto_approve")
          return PolicyAction::AutoApprove;
        if (action == "deny")
          return PolicyAction::Deny;
        if (action == "ask")
          return PolicyAction::Ask;
      }
    }

    // 2. 匹配通配符 "*" 作为 fallback
    if (config.contains("*")) {
      const auto &value = config["*"];
      if (value.is_string()) {
        const std::string action = value.get<std::string>();
        if (action == "auto_approve")
          return PolicyAction::AutoApprove;
        if (action == "deny")
          return PolicyAction::Deny;
        if (action == "ask")
          return PolicyAction::Ask;
      }
    }

  } catch (const nlohmann::json::exception &) {
    // 解析失败 → 默认走标准流程
  }

  return PolicyAction::Ask;
}

} // namespace codepilot