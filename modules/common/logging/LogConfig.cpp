#include "LogConfig.h"

#include <fstream>
#include <sstream>
#include <stdexcept>

// 引用第三方 json 头文件
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace codepilot {
namespace log {

// ============================================================
// 辅助：字符串 → Level
// ============================================================
Level levelFromString(const std::string &s) {
  if (s == "trace")
    return Level::Trace;
  if (s == "debug")
    return Level::Debug;
  if (s == "info")
    return Level::Info;
  if (s == "warn")
    return Level::Warn;
  if (s == "error")
    return Level::Error;
  if (s == "critical")
    return Level::Critical;
  if (s == "off")
    return Level::Off;
  return Level::Debug; // 默认
}

// ============================================================
// 辅助：字符串 → SinkType
// ============================================================
SinkType sinkTypeFromString(const std::string &s) {
  if (s == "console")
    return SinkType::Console;
  if (s == "file" || s == "rotating_file")
    return SinkType::RotatingFile;
  if (s == "sqlite")
    return SinkType::Sqlite;
  return SinkType::Console; // 默认
}

// ============================================================
// 默认配置
// ============================================================
GlobalConfig defaultConfig() {
  GlobalConfig cfg;

  LoggerConfig defaultLogger;
  defaultLogger.name = "default";
  defaultLogger.level = Level::Debug;
  defaultLogger.sinks = {SinkType::Console, SinkType::RotatingFile};
  defaultLogger.pattern = "[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%n] %v";
  defaultLogger.filePath = "/app/logs/codepilot.log";
  defaultLogger.maxFileSizeMb = 10;
  defaultLogger.maxFiles = 5;
  cfg.loggers.push_back(defaultLogger);

  return cfg;
}

// ============================================================
// 从 JSON 字符串加载配置
// ============================================================
GlobalConfig loadConfigFromJson(const std::string &jsonStr) {
  GlobalConfig cfg;
  json j;

  try {
    j = json::parse(jsonStr);
  } catch (...) {
    // JSON 解析失败，返回默认配置
    return defaultConfig();
  }

  // 解析 loggers 数组
  if (!j.contains("loggers") || !j["loggers"].is_array()) {
    return defaultConfig();
  }

  for (const auto &loggerJson : j["loggers"]) {
    LoggerConfig lcfg;

    if (loggerJson.contains("name"))
      lcfg.name = loggerJson["name"].get<std::string>();

    if (loggerJson.contains("level"))
      lcfg.level = levelFromString(loggerJson["level"].get<std::string>());

    if (loggerJson.contains("pattern"))
      lcfg.pattern = loggerJson["pattern"].get<std::string>();

    if (loggerJson.contains("file_path"))
      lcfg.filePath = loggerJson["file_path"].get<std::string>();

    if (loggerJson.contains("max_file_size_mb"))
      lcfg.maxFileSizeMb = loggerJson["max_file_size_mb"].get<int>();

    if (loggerJson.contains("max_files"))
      lcfg.maxFiles = loggerJson["max_files"].get<int>();

    // 解析 sinks 数组
    if (loggerJson.contains("sinks") && loggerJson["sinks"].is_array()) {
      lcfg.sinks.clear();
      for (const auto &sinkStr : loggerJson["sinks"]) {
        lcfg.sinks.push_back(sinkTypeFromString(sinkStr.get<std::string>()));
      }
    }

    // 如果某个 logger 既需要 console 又需要 file，
    // 使用 RotatingFile sink 并保留 console
    // （sinks 已通过 JSON 控制）

    cfg.loggers.push_back(lcfg);
  }

  // 如果没有任何 logger 配置，添加默认
  if (cfg.loggers.empty()) {
    return defaultConfig();
  }

  return cfg;
}

// ============================================================
// 从文件加载配置
// ============================================================
GlobalConfig loadConfigFromFile(const std::string &path) {
  std::ifstream file(path);
  if (!file.is_open()) {
    return defaultConfig();
  }

  std::stringstream buffer;
  buffer << file.rdbuf();
  return loadConfigFromJson(buffer.str());
}

} // namespace log
} // namespace codepilot