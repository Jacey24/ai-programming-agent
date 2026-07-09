#include "Logger.h"

#include <spdlog/async.h>
#include <spdlog/sinks/null_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>


#include <mutex>
#include <unordered_map>

namespace codepilot {
namespace log {

namespace {

// ============================================================
// 内部状态
// ============================================================
std::mutex gMutex;
std::unordered_map<std::string, std::shared_ptr<spdlog::logger>> gLoggers;
std::shared_ptr<spdlog::logger> gDefaultLogger;
bool gInitialized = false;

// ============================================================
// 创建 spdlog sink
// ============================================================
std::shared_ptr<spdlog::sinks::sink> createSink(SinkType type,
                                                const LoggerConfig &cfg) {
  switch (type) {
  case SinkType::Console:
    return std::make_shared<spdlog::sinks::stdout_color_sink_mt>();

  case SinkType::RotatingFile: {
    // 确保文件路径存在
    auto sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        cfg.filePath, cfg.maxFileSizeMb * 1024 * 1024, cfg.maxFiles);
    return sink;
  }

  case SinkType::Sqlite:
    // TODO Sprint 3: 实现 SQLite sink
    // 当前使用 null sink 占位，先不阻塞编译
    return std::make_shared<spdlog::sinks::null_sink_mt>();

  default:
    return std::make_shared<spdlog::sinks::null_sink_mt>();
  }
}

// ============================================================
// 将 codepilot::log::Level 映射为 spdlog::level::level_enum
// ============================================================
spdlog::level::level_enum toSpdlogLevel(Level level) {
  switch (level) {
  case Level::Trace:
    return spdlog::level::trace;
  case Level::Debug:
    return spdlog::level::debug;
  case Level::Info:
    return spdlog::level::info;
  case Level::Warn:
    return spdlog::level::warn;
  case Level::Error:
    return spdlog::level::err;
  case Level::Critical:
    return spdlog::level::critical;
  case Level::Off:
    return spdlog::level::off;
  default:
    return spdlog::level::debug;
  }
}

} // anonymous namespace

// ============================================================
// init — 从 GlobalConfig 初始化
// ============================================================
void init(const GlobalConfig &config) {
  std::lock_guard<std::mutex> lock(gMutex);

  // 如果已经初始化，先关闭旧的
  if (gInitialized) {
    shutdown();
  }

  for (const auto &loggerCfg : config.loggers) {
    // 收集该 logger 的所有 sink
    std::vector<std::shared_ptr<spdlog::sinks::sink>> sinks;
    for (auto sinkType : loggerCfg.sinks) {
      sinks.push_back(createSink(sinkType, loggerCfg));
    }

    // 如果没有任何 sink，给一个 null sink 兜底
    if (sinks.empty()) {
      sinks.push_back(std::make_shared<spdlog::sinks::null_sink_mt>());
    }

    // 创建 logger
    auto logger = std::make_shared<spdlog::logger>(loggerCfg.name,
                                                   sinks.begin(), sinks.end());

    // 设置级别
    logger->set_level(toSpdlogLevel(loggerCfg.level));

    // 设置格式
    if (!loggerCfg.pattern.empty()) {
      logger->set_pattern(loggerCfg.pattern);
    }

    // 注册
    spdlog::register_logger(logger);
    gLoggers[loggerCfg.name] = logger;

    // 第一个注册的 logger 作为默认
    if (!gDefaultLogger) {
      gDefaultLogger = logger;
    }
  }

  gInitialized = true;
}

// ============================================================
// initFromFile — 从配置 JSON 文件初始化
// ============================================================
void initFromFile(const std::string &configPath) {
  auto config = loadConfigFromFile(configPath);
  init(config);
}

// ============================================================
// initFromDefault — 使用默认配置初始化
// ============================================================
void initFromDefault() {
  auto config = defaultConfig();
  init(config);
}

// ============================================================
// shutdown — 关闭所有 logger
// ============================================================
void shutdown() {
  std::lock_guard<std::mutex> lock(gMutex);
  spdlog::shutdown();
  gLoggers.clear();
  gDefaultLogger.reset();
  gInitialized = false;
}

// ============================================================
// get — 获取具名 logger
// ============================================================
std::shared_ptr<spdlog::logger> get(const std::string &name) {
  std::lock_guard<std::mutex> lock(gMutex);
  auto it = gLoggers.find(name);
  if (it != gLoggers.end()) {
    return it->second;
  }
  // 未找到则返回默认
  return gDefaultLogger;
}

// ============================================================
// getDefault — 获取默认 logger
// ============================================================
std::shared_ptr<spdlog::logger> getDefault() {
  std::lock_guard<std::mutex> lock(gMutex);
  return gDefaultLogger;
}

// ============================================================
// setLevel — 设置特定 logger 的级别
// ============================================================
void setLevel(const std::string &name, Level level) {
  auto l = get(name);
  if (l) {
    l->set_level(toSpdlogLevel(level));
  }
}

// ============================================================
// setLevel — 全局设置级别
// ============================================================
void setLevel(Level level) {
  auto spdLevel = toSpdlogLevel(level);
  std::lock_guard<std::mutex> lock(gMutex);
  for (auto &[name, logger] : gLoggers) {
    logger->set_level(spdLevel);
  }
}

// ============================================================
// exists
// ============================================================
bool exists(const std::string &name) {
  std::lock_guard<std::mutex> lock(gMutex);
  return gLoggers.find(name) != gLoggers.end();
}

} // namespace log
} // namespace codepilot