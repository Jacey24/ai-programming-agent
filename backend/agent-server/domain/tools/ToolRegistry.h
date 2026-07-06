#pragma once

#include "Tool.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace codepilot {

// ============================================================
// ToolRegistry - 工具注册中心
// 负责：工具注册、查询、调用、schema 聚合
// ============================================================
class ToolRegistry {
public:
  // --- 注册工具 ---
  void registerTool(std::unique_ptr<Tool> tool);

  // --- 按名称查询工具（指针，nullptr 表示不存在） ---
  Tool *getTool(const std::string &name) const;

  // --- 检查工具是否已注册 ---
  bool hasTool(const std::string &name) const;

  // --- 获取所有已注册的工具名称 ---
  std::vector<std::string> listToolNames() const;

  // --- 调用工具（核心入口）---
  ToolResult call(const std::string &name, const ToolContext &context,
                  const json &arguments);

  // --- 批量获取 schema（供 LLM 生成 tool_call schema）---
  json listSchemas() const;

  // --- 批量获取轻量摘要（渐进式提示词）---
  json listSummaries() const;

  // --- 获取工具信息列表（供 API 层使用，对应 GET /api/v1/tools）---
  json listToolInfo() const;

  // --- 获取工具详情（对应 GET /api/v1/tools/{name}）---
  json getToolDetail(const std::string &name) const;

  // --- 获取注册工具数量 ---
  size_t size() const { return tools_.size(); }

private:
  std::unordered_map<std::string, std::unique_ptr<Tool>> tools_;
};

} // namespace codepilot