#include "ExpertRegistry.h"
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>


namespace codepilot {

using json = nlohmann::json;

bool ExpertRegistry::loadFromFile(const std::string &configPath) {
  std::ifstream file(configPath);
  if (!file.is_open())
    return false;
  std::stringstream buf;
  buf << file.rdbuf();
  return loadFromJson(buf.str());
}

bool ExpertRegistry::loadFromJson(const std::string &jsonStr) {
  try {
    json j = json::parse(jsonStr);

    if (j.contains("experts") && j["experts"].is_array()) {
      for (const auto &e : j["experts"]) {
        experts_.push_back(ExpertConfig::fromJson(e));
      }
      return !experts_.empty();
    }

    // 直接数组格式
    if (j.is_array()) {
      for (const auto &e : j) {
        experts_.push_back(ExpertConfig::fromJson(e));
      }
      return !experts_.empty();
    }

    return false;
  } catch (const std::exception &) {
    return false;
  }
}

const ExpertConfig *ExpertRegistry::findByName(const std::string &name) const {
  for (const auto &e : experts_) {
    if (e.name == name)
      return &e;
  }
  return nullptr;
}

const ExpertConfig *ExpertRegistry::getEntryExpert() const {
  for (const auto &e : experts_) {
    if (e.isEntry)
      return &e;
  }
  // 如果没有标记 isEntry，返回第一个
  return experts_.empty() ? nullptr : &experts_.front();
}

std::vector<std::string> ExpertRegistry::listNames() const {
  std::vector<std::string> names;
  for (const auto &e : experts_) {
    names.push_back(e.name);
  }
  return names;
}

} // namespace codepilot