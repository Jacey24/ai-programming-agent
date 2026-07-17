#include "EventPrinter.h"
#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

EventPrinter::EventPrinter(bool verbose) : verbose_(verbose) {
#ifdef _WIN32
  // 启用 ANSI 转义序列支持
  HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
  DWORD dwMode = 0;
  GetConsoleMode(hOut, &dwMode);
  dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
  SetConsoleMode(hOut, dwMode);
#endif
}

const char *EventPrinter::colorDialog() { return "\033[36m"; }
const char *EventPrinter::colorStatus() { return "\033[34m"; }
const char *EventPrinter::colorDebug() { return "\033[90m"; }
const char *EventPrinter::colorTime() { return "\033[90m"; }
const char *EventPrinter::colorReset() { return "\033[0m"; }
const char *EventPrinter::colorGreen() { return "\033[32m"; }
const char *EventPrinter::colorRed() { return "\033[31m"; }
const char *EventPrinter::colorYellow() { return "\033[33m"; }

std::string EventPrinter::timestamp() const {
  auto t =
      std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  std::tm *tm = std::localtime(&t);
  std::ostringstream oss;
  oss << std::setfill('0') << std::setw(2) << tm->tm_hour << ":" << std::setw(2)
      << tm->tm_min << ":" << std::setw(2) << tm->tm_sec;
  return oss.str();
}

void EventPrinter::printColored(const std::string &prefix,
                                const std::string &content, const char *color) {
  std::cout << colorTime() << "[" << timestamp() << "]" << colorReset() << " "
            << color << prefix << colorReset() << " " << content << std::endl;
}

static std::string trim(const std::string &s) {
  auto start = s.find_first_not_of(" \t\n\r");
  if (start == std::string::npos)
    return "";
  auto end = s.find_last_not_of(" \t\n\r");
  return s.substr(start, end - start + 1);
}

void EventPrinter::onEvent(const json &event) {
  std::string type = event.value("type", "unknown");
  std::string content = event.value("content", "");
  json metadata = event.value("metadata", json::object());
  std::string channel = metadata.value("channel", "status");

  std::cerr << "[EVENT TRACE] onEvent: type=" << type << " channel=" << channel
            << " content_len=" << content.size() << std::endl;

  // 过滤心跳和流结束
  if (type == "stream_end") {
    std::cerr << "[EVENT TRACE] filtered: stream_end" << std::endl;
    return;
  }

  if (channel == "dialog") {
    bool streaming = metadata.value("streaming", false);
    bool streamEnd = metadata.value("stream_end", false);
    if (!streaming || streamEnd) {
      std::string trimmed = trim(content);
      std::cerr << "[EVENT TRACE] dialog: streaming=" << streaming
                << " streamEnd=" << streamEnd
                << " trimmed_len=" << trimmed.size() << std::endl;
      if (!trimmed.empty()) {
        printColored("[assistant]", trimmed, colorDialog());
      } else {
        std::cerr << "[EVENT TRACE] dialog: trimmed empty, skipped print"
                  << std::endl;
      }
    } else {
      std::cerr << "[EVENT TRACE] dialog: skipped (streaming chunk)"
                << std::endl;
    }
  } else if (channel == "status") {
    std::string stage = metadata.value("stage", "");
    std::string expert = metadata.value("expert", "");

    std::cerr << "[EVENT TRACE] status: stage=" << stage << " expert=" << expert
              << std::endl;

    if (type == "task_completed") {
      std::cout << std::endl;
      printColored("[status]", "任务完成", colorGreen());
      std::cerr << "[EVENT TRACE] status: printed task_completed" << std::endl;
    } else if (type == "task_failed") {
      std::cout << std::endl;
      printColored("[status]",
                   metadata.value("status", "") == "interrupted"
                       ? "任务中断"
                       : "任务失败",
                   colorRed());
      std::cerr << "[EVENT TRACE] status: printed task_failed" << std::endl;
    } else if (type == "task_cancelled") {
      std::cout << std::endl;
      printColored("[status]", "任务已取消", colorYellow());
      std::cerr << "[EVENT TRACE] status: printed task_cancelled" << std::endl;
    } else if (!stage.empty()) {
      std::string line = trim(content);
      std::cout << colorTime() << "[" << timestamp() << "]" << colorReset()
                << " " << colorStatus() << "[status]" << colorReset() << " "
                << line << std::endl;
      std::cerr << "[EVENT TRACE] status: printed stage line" << std::endl;
    } else {
      std::cerr << "[EVENT TRACE] status: no stage, skipped" << std::endl;
    }
  } else if (channel == "debug" && verbose_) {
    std::string source = metadata.value("source", "");
    std::string toolName = metadata.value("tool_name", "");
    std::string line = trim(content);
    if (!toolName.empty()) {
      line = "[" + toolName + "] " + line;
    }
    printColored("[debug]", line, colorDebug());
    std::cerr << "[EVENT TRACE] debug: printed" << std::endl;
  } else if (channel == "debug" && !verbose_) {
    std::cerr << "[EVENT TRACE] debug: skipped (verbose off)" << std::endl;
  }
  std::cerr << "[EVENT TRACE] onEvent DONE" << std::endl;
}
