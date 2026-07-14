#pragma once

#include <nlohmann/json.hpp>
#include <string>


using json = nlohmann::json;

// ============================================================
// EventPrinter — SSE 事件的格式化彩色终端输出
// 按 metadata.channel 路由到不同视觉区域
// ============================================================
class EventPrinter {
public:
  EventPrinter(bool verbose = false);

  void setVerbose(bool verbose) { verbose_ = verbose; }

  // 处理单个 SSE 事件并输出到终端
  void onEvent(const json &event);

private:
  bool verbose_;
  bool seenSummary_{false};

  // ANSI 颜色码
  static const char *colorDialog();
  static const char *colorStatus();
  static const char *colorDebug();
  static const char *colorTime();
  static const char *colorReset();
  static const char *colorGreen();
  static const char *colorRed();
  static const char *colorYellow();

  std::string timestamp() const;
  void printColored(const std::string &prefix, const std::string &content,
                    const char *color);
};