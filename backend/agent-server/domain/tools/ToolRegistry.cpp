#include "ToolRegistry.h"

namespace codepilot {

void ToolRegistry::registerTool(std::unique_ptr<Tool> tool) {
  if (!tool)
    return;
  std::string name = tool->name();
  std::string groupName = tool->group();
  tools_[name] = std::move(tool);

  // 自动将工具加入对应的分组
  auto it = groups_.find(groupName);
  if (it != groups_.end()) {
    // 避免重复添加
    bool found = false;
    for (const auto &tn : it->second.toolNames) {
      if (tn == name) {
        found = true;
        break;
      }
    }
    if (!found) {
      it->second.toolNames.push_back(name);
    }
  } else {
    // 自动创建分组
    ToolGroupInfo info;
    info.name = groupName;
    info.toolNames.push_back(name);
    groups_[groupName] = info;
  }
}

Tool *ToolRegistry::getTool(const std::string &name) const {
  auto it = tools_.find(name);
  if (it == tools_.end())
    return nullptr;
  return it->second.get();
}

bool ToolRegistry::hasTool(const std::string &name) const {
  return tools_.find(name) != tools_.end();
}

std::vector<std::string> ToolRegistry::listToolNames() const {
  std::vector<std::string> names;
  names.reserve(tools_.size());
  for (const auto &[name, _] : tools_) {
    names.push_back(name);
  }
  return names;
}

ToolResult ToolRegistry::call(const std::string &name,
                              const ToolContext &context,
                              const json &arguments) {
  auto it = tools_.find(name);
  if (it == tools_.end()) {
    // FallbackHandler: 友好提示可用工具
    std::string msg = "未知工具 '" + name + "'。";
    msg += "可用工具分组：\n" + listAvailableToolsByGroup();
    msg += "\n请检查工具名是否正确。";
    return ToolResult::Err(msg);
  }

  Tool *tool = it->second.get();

  // 1. 参数校验
  auto errors = tool->validate(arguments);
  if (!errors.empty()) {
    std::string errorMsg = "Validation failed for tool '" + name + "': ";
    for (size_t i = 0; i < errors.size(); ++i) {
      if (i > 0)
        errorMsg += "; ";
      errorMsg += errors[i].field + ": " + errors[i].message;
    }
    return ToolResult::Err(errorMsg);
  }

  // 2. 执行工具
  try {
    return tool->execute(context, arguments);
  } catch (const std::exception &e) {
    return ToolResult::Err("Tool execution exception: " +
                           std::string(e.what()));
  }
}

json ToolRegistry::listSchemas() const {
  json result;
  result["tools"] = json::array();
  for (const auto &[name, tool] : tools_) {
    result["tools"].push_back(tool->schema().toOpenAISchema());
  }
  return result;
}

json ToolRegistry::listSchemas(const std::string &group) const {
  json result;
  result["tools"] = json::array();
  for (const auto &[name, tool] : tools_) {
    if (tool->group() == group) {
      result["tools"].push_back(tool->schema().toOpenAISchema());
    }
  }
  return result;
}

json ToolRegistry::listSummaries() const {
  json result;
  result["tools"] = json::array();
  for (const auto &[name, tool] : tools_) {
    result["tools"].push_back(tool->summary());
  }
  return result;
}

json ToolRegistry::listToolInfo() const {
  // 对应 GET /api/v1/tools 接口的返回格式
  json result;
  result["items"] = json::array();
  for (const auto &[name, tool] : tools_) {
    json item;
    item["name"] = tool->name();
    item["description"] = tool->description();
    item["group"] = tool->group();
    json dummy;
    item["risk_level"] = riskLevelToString(tool->riskLevel(dummy));
    item["params"] = json::object();
    for (const auto &param : tool->schema().params) {
      item["params"][param.name] = {
          {"type", param.type},
          {"description", param.description},
          {"required", param.required}};
    }
    result["items"].push_back(item);
  }
  return result;
}

json ToolRegistry::listToolInfo(const std::string &group) const {
  json result;
  result["items"] = json::array();
  for (const auto &[name, tool] : tools_) {
    if (tool->group() == group) {
      json item;
      item["name"] = tool->name();
      item["description"] = tool->description();
      item["group"] = tool->group();
      json dummy;
      item["risk_level"] = riskLevelToString(tool->riskLevel(dummy));
      item["params"] = json::object();
      for (const auto &param : tool->schema().params) {
        item["params"][param.name] = {
            {"type", param.type},
            {"description", param.description},
            {"required", param.required}};
      }
      result["items"].push_back(item);
    }
  }
  return result;
}

json ToolRegistry::getToolDetail(const std::string &name) const {
  // 对应 GET /api/v1/tools/{name} 接口
  json result;
  auto *tool = getTool(name);
  if (!tool) {
    return result; // 返回空对象，Controller 层处理 404
  }
  result["name"] = tool->name();
  result["description"] = tool->description();
  result["group"] = tool->group();
  json dummy;
  result["risk_level"] = riskLevelToString(tool->riskLevel(dummy));
  result["schema"] = tool->schema().toOpenAISchema();
  return result;
}

// ============================================================
// 分组管理实现
// ============================================================

std::vector<std::string> ToolRegistry::listGroupNames() const {
  std::vector<std::string> names;
  names.reserve(groups_.size());
  for (const auto &[gname, _] : groups_) {
    names.push_back(gname);
  }
  return names;
}

void ToolRegistry::registerGroup(const std::string &groupName,
                                 const std::string &promptSnippet) {
  auto it = groups_.find(groupName);
  if (it != groups_.end()) {
    it->second.promptSnippet = promptSnippet;
  } else {
    ToolGroupInfo info;
    info.name = groupName;
    info.promptSnippet = promptSnippet;
    groups_[groupName] = info;
  }
}

ToolGroupInfo *ToolRegistry::getGroupInfo(const std::string &groupName) {
  auto it = groups_.find(groupName);
  if (it == groups_.end())
    return nullptr;
  return &it->second;
}

std::string ToolRegistry::getGroupPrompt(const std::string &groupName) const {
  auto it = groups_.find(groupName);
  if (it == groups_.end())
    return "";
  return it->second.promptSnippet;
}

std::string ToolRegistry::listAvailableToolsByGroup() const {
  std::ostringstream oss;
  for (const auto &[gname, info] : groups_) {
    oss << "  [" << gname << " 组] ";
    for (size_t i = 0; i < info.toolNames.size(); ++i) {
      if (i > 0)
        oss << ", ";
      oss << info.toolNames[i];
    }
    oss << "\n";
  }
  return oss.str();
}

} // namespace codepilot
