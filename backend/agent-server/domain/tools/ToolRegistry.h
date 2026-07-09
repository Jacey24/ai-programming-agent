#pragma once

#include "Tool.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace codepilot {

// ============================================================
// 工具分组注册信息（用于 group 级别的元数据）
// ============================================================
struct ToolGroupInfo {
  std::string name;          // "git" / "file" / "shell" / ...
  std::string promptSnippet; // 组级提示词片段，Agent 可注入 System Prompt
  std::vector<std::string> toolNames;
};

// ============================================================
// ToolRegistry - 工具注册中心
// 负责：工具注册、查询、调用、schema 聚合、分组管理
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

  // --- 按分组获取 schema ---
  json listSchemas(const std::string &group) const;

  // --- 批量获取轻量摘要（渐进式提示词）---
  json listSummaries() const;

  // --- 获取工具信息列表（供 API 层使用，对应 GET /api/v1/tools）---
  json listToolInfo() const;

  // --- 按分组获取工具信息列表 ---
  json listToolInfo(const std::string &group) const;

  // --- 获取工具详情（对应 GET /api/v1/tools/{name}）---
  json getToolDetail(const std::string &name) const;

  // --- 分组管理 ---

  // 获取所有分组名称
  std::vector<std::string> listGroupNames() const;

  // 注册一个分组（含组级提示词）
  void registerGroup(const std::string &groupName,
                     const std::string &promptSnippet);

  // 获取分组信息
  ToolGroupInfo *getGroupInfo(const std::string &groupName);

  // 获取某个分组的提示词片段
  std::string getGroupPrompt(const std::string &groupName) const;

  // --- 获取注册工具数量 ---
  size_t size() const { return tools_.size(); }

  // --- 获取所有分组名列表（用于 fallback 提示）---
  std::string listAvailableToolsByGroup() const;

private:
  std::unordered_map<std::string, std::unique_ptr<Tool>> tools_;
  std::unordered_map<std::string, ToolGroupInfo> groups_;
};

} // namespace codepilot