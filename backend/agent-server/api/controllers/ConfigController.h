#pragma once

#include <string>

namespace codepilot {

// ConfigController — 全局配置文件读写（agent.json / llm.json / logging.json /
// workspace.json）
class ConfigController {
public:
  // ── 全局配置合并视图 ──
  std::string getConfig() const;

  // ── agent.json ──
  std::string getAgentConfig() const;
  std::string setAgentConfig(const std::string &request) const;

  // ── llm.json ──
  std::string getLlmConfig() const;
  std::string setLlmConfig(const std::string &request) const;
  std::string testLlmConnection(const std::string &request) const;

  // ── llm.json provider 管理 ──
  std::string listLlmProviders() const;
  std::string addLlmProvider(const std::string &request) const;
  std::string updateLlmProvider(const std::string &request) const;
  std::string deleteLlmProvider(const std::string &request) const;

  // ── workspace.json ──
  std::string getWorkspaceConfig() const;
  std::string setWorkspaceConfig(const std::string &request) const;

  // ── logging.json ──
  std::string getLoggingConfig() const;
  std::string setLoggingConfig(const std::string &request) const;

  // ── tools.json ──
  std::string getToolsConfig() const;
  std::string setToolsConfig(const std::string &request) const;

  // ── llm.local.json ──
  std::string getLlmLocalConfig() const;
  std::string setLlmLocalConfig(const std::string &request) const;
};

} // namespace codepilot