#pragma once

#include <string>
#include <vector>

namespace codepilot {
namespace log {

// ============================================================
// 日志级别
// ============================================================
enum class Level { Trace, Debug, Info, Warn, Error, Critical, Off };

// ============================================================
// Sink 类型
// ============================================================
enum class SinkType { Console, RotatingFile, Sqlite };

// ============================================================
// 单个 Logger 配置
// ============================================================
struct LoggerConfig {
  std::string name{"default"};
  Level level{Level::Debug};
  std::vector<SinkType> sinks{SinkType::Console};
  std::string pattern{"[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%n] %v"};

  // 文件相关（仅 RotatingFile 有效）
  std::string filePath{"/app/logs/codepilot.log"};
  int maxFileSizeMb{10};
  int maxFiles{5};
};

// ============================================================
// 全局日志配置
// ============================================================
struct GlobalConfig {
  std::vector<LoggerConfig> loggers;
};

// ============================================================
// 从 JSON 反序列化配置
// ============================================================
GlobalConfig loadConfigFromFile(const std::string &path);
GlobalConfig loadConfigFromJson(const std::string &jsonStr);
GlobalConfig defaultConfig();

// ============================================================
// 辅助：字符串 ↔ 枚举
// ============================================================
Level levelFromString(const std::string &s);
SinkType sinkTypeFromString(const std::string &s);

} // namespace log
} // namespace codepilot