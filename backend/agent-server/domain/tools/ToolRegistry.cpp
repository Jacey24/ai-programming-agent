#include "ToolRegistry.h"

namespace codepilot {

void ToolRegistry::registerTool(std::unique_ptr<Tool> tool) {
  if (!tool)
    return;
  std::string name = tool->name();
  tools_[name] = std::move(tool);
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
    return ToolResult::Err("Tool not found: " + name);
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
    json dummy;
    item["risk_level"] = riskLevelToString(tool->riskLevel(dummy));
    result["items"].push_back(item);
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
  json dummy;
  result["risk_level"] = riskLevelToString(tool->riskLevel(dummy));
  result["schema"] = tool->schema().toOpenAISchema();
  return result;
}

} // namespace codepilot