#pragma once

#include <string>

namespace codepilot {

class ExpertController {
public:
  // ── 查询 ──
  std::string listExperts() const;
  std::string getExpert(const std::string &request) const;
  std::string getGraph(const std::string &request) const;
  std::string exportConfig() const;

  // ── CRUD ──
  std::string createExpert(const std::string &request) const;
  std::string updateExpert(const std::string &request) const;
  std::string patchExpert(const std::string &request) const;
  std::string deleteExpert(const std::string &request) const;

  // ── 子资源 ──
  std::string setTools(const std::string &request) const;
  std::string addTool(const std::string &request) const;
  std::string removeTool(const std::string &request) const;
  std::string setRoutes(const std::string &request) const;
  std::string addRoute(const std::string &request) const;
  std::string removeRoute(const std::string &request) const;
  std::string setLlm(const std::string &request) const;

  // ── 导入/验证 ──
  std::string importConfig(const std::string &request) const;
  std::string validateConfig(const std::string &request) const;

  // ── 提示词预览 ──
  std::string previewPrompt(const std::string &request) const;

  // ── 画布位置 ──
  std::string savePositions(const std::string &request) const;
  std::string getPositions(const std::string &request) const;

  // ── 全局 LLM 默认值 ──
  std::string getGlobalLlmDefaults() const;
  std::string setGlobalLlmDefaults(const std::string &request) const;
};

} // namespace codepilot
