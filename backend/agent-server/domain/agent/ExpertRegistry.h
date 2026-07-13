#pragma once

#include "ExpertConfig.h"
#include <string>
#include <vector>

namespace codepilot {

// ============================================================
// ExpertRegistry — Expert 注册表
// 从 JSON 文件加载 Expert 配置，替代旧的 RoleRegistry
// ============================================================
class ExpertRegistry {
public:
  // 从 JSON 文件加载 Expert 配置
  bool loadFromFile(const std::string &configPath);

  // 从 JSON 字符串加载
  bool loadFromJson(const std::string &jsonStr);

  // 查询
  const ExpertConfig *findByName(const std::string &name) const;
  const ExpertConfig *getEntryExpert() const;
  std::vector<std::string> listNames() const;
  size_t count() const { return experts_.size(); }

  // 检查是否已加载
  bool isLoaded() const { return !experts_.empty(); }

private:
  std::vector<ExpertConfig> experts_;
};

} // namespace codepilot