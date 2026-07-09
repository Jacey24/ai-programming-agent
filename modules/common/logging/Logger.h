#pragma once

#include "LogConfig.h"

#include <memory>
#include <string>

// ============================================================
// 直接包含 spdlog（header-only，无需编译）
// 宏展开时需要完整的 spdlog 类型定义
// ============================================================
#include <spdlog/spdlog.h>

namespace codepilot {
namespace log {

// ============================================================
// 初始化/关闭
// ============================================================
void init(const GlobalConfig &config);
void initFromFile(const std::string &configPath);
void initFromDefault();
void shutdown();

// ============================================================
// 获取具名 Logger / 默认 Logger
// ============================================================
std::shared_ptr<spdlog::logger> get(const std::string &name);
std::shared_ptr<spdlog::logger> getDefault();

// ============================================================
// 运行时动态调整
// ============================================================
void setLevel(const std::string &name, Level level);
void setLevel(Level level); // 全局设置

// ============================================================
// 检查某 logger 是否已注册
// ============================================================
bool exists(const std::string &name);

} // namespace log
} // namespace codepilot

// ============================================================
// 便捷宏 — 所有组员只需 include "Logger.h" 即可使用
//
// 用法示例：
//   LOG_INFO("Tool {} executed, result={}", toolName, result);
//   LOG_ERROR("Failed: {}", err.what());
//
// 自动注入文件名、行号、函数名
// ============================================================
#define LOG_TRACE(...)                                                         \
  do {                                                                         \
    auto _l = ::codepilot::log::getDefault();                                  \
    if (_l && _l->should_log(spdlog::level::trace)) {                          \
      _l->log(spdlog::source_loc{__FILE__, __LINE__, __FUNCTION__},            \
              spdlog::level::trace, __VA_ARGS__);                              \
    }                                                                          \
  } while (0)

#define LOG_DEBUG(...)                                                         \
  do {                                                                         \
    auto _l = ::codepilot::log::getDefault();                                  \
    if (_l && _l->should_log(spdlog::level::debug)) {                          \
      _l->log(spdlog::source_loc{__FILE__, __LINE__, __FUNCTION__},            \
              spdlog::level::debug, __VA_ARGS__);                              \
    }                                                                          \
  } while (0)

#define LOG_INFO(...)                                                          \
  do {                                                                         \
    auto _l = ::codepilot::log::getDefault();                                  \
    if (_l && _l->should_log(spdlog::level::info)) {                           \
      _l->log(spdlog::source_loc{__FILE__, __LINE__, __FUNCTION__},            \
              spdlog::level::info, __VA_ARGS__);                               \
    }                                                                          \
  } while (0)

#define LOG_WARN(...)                                                          \
  do {                                                                         \
    auto _l = ::codepilot::log::getDefault();                                  \
    if (_l && _l->should_log(spdlog::level::warn)) {                           \
      _l->log(spdlog::source_loc{__FILE__, __LINE__, __FUNCTION__},            \
              spdlog::level::warn, __VA_ARGS__);                               \
    }                                                                          \
  } while (0)

#define LOG_ERROR(...)                                                         \
  do {                                                                         \
    auto _l = ::codepilot::log::getDefault();                                  \
    if (_l && _l->should_log(spdlog::level::err)) {                            \
      _l->log(spdlog::source_loc{__FILE__, __LINE__, __FUNCTION__},            \
              spdlog::level::err, __VA_ARGS__);                                \
    }                                                                          \
  } while (0)

#define LOG_CRITICAL(...)                                                      \
  do {                                                                         \
    auto _l = ::codepilot::log::getDefault();                                  \
    if (_l && _l->should_log(spdlog::level::critical)) {                       \
      _l->log(spdlog::source_loc{__FILE__, __LINE__, __FUNCTION__},            \
              spdlog::level::critical, __VA_ARGS__);                           \
    }                                                                          \
  } while (0)

// ============================================================
// 具名 Logger 宏（用于模块级独立控制）
// 用法：
//   LOGN_INFO("ToolSystem", "Tool {} executed", toolName);
// ============================================================
#define LOGN_TRACE(name, ...)                                                  \
  do {                                                                         \
    auto _l = ::codepilot::log::get(name);                                     \
    if (_l && _l->should_log(spdlog::level::trace)) {                          \
      _l->log(spdlog::source_loc{__FILE__, __LINE__, __FUNCTION__},            \
              spdlog::level::trace, __VA_ARGS__);                              \
    }                                                                          \
  } while (0)

#define LOGN_DEBUG(name, ...)                                                  \
  do {                                                                         \
    auto _l = ::codepilot::log::get(name);                                     \
    if (_l && _l->should_log(spdlog::level::debug)) {                          \
      _l->log(spdlog::source_loc{__FILE__, __LINE__, __FUNCTION__},            \
              spdlog::level::debug, __VA_ARGS__);                              \
    }                                                                          \
  } while (0)

#define LOGN_INFO(name, ...)                                                   \
  do {                                                                         \
    auto _l = ::codepilot::log::get(name);                                     \
    if (_l && _l->should_log(spdlog::level::info)) {                           \
      _l->log(spdlog::source_loc{__FILE__, __LINE__, __FUNCTION__},            \
              spdlog::level::info, __VA_ARGS__);                               \
    }                                                                          \
  } while (0)

#define LOGN_WARN(name, ...)                                                   \
  do {                                                                         \
    auto _l = ::codepilot::log::get(name);                                     \
    if (_l && _l->should_log(spdlog::level::warn)) {                           \
      _l->log(spdlog::source_loc{__FILE__, __LINE__, __FUNCTION__},            \
              spdlog::level::warn, __VA_ARGS__);                               \
    }                                                                          \
  } while (0)

#define LOGN_ERROR(name, ...)                                                  \
  do {                                                                         \
    auto _l = ::codepilot::log::get(name);                                     \
    if (_l && _l->should_log(spdlog::level::err)) {                            \
      _l->log(spdlog::source_loc{__FILE__, __LINE__, __FUNCTION__},            \
              spdlog::level::err, __VA_ARGS__);                                \
    }                                                                          \
  } while (0)

#define LOGN_CRITICAL(name, ...)                                               \
  do {                                                                         \
    auto _l = ::codepilot::log::get(name);                                     \
    if (_l && _l->should_log(spdlog::level::critical)) {                       \
      _l->log(spdlog::source_loc{__FILE__, __LINE__, __FUNCTION__},            \
              spdlog::level::critical, __VA_ARGS__);                           \
    }                                                                          \
  } while (0)