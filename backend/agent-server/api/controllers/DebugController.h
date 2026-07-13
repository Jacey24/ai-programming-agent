#pragma once

#include <string>

namespace codepilot {

class DebugController {
public:
  // ── 启停控制 ──
  std::string setEnabled(const std::string &request) const;

  // ── 断点管理 ──
  std::string setBreakpoint(const std::string &request) const;
  std::string removeBreakpoint(const std::string &request) const;
  std::string listBreakpoints() const;

  // ── 执行控制 ──
  std::string doContinue(const std::string &request) const;
  std::string doStepOver(const std::string &request) const;
  std::string doSkip(const std::string &request) const;

  // ── 参数/结果修改 ──
  std::string modifyArguments(const std::string &request) const;
  std::string modifyResult(const std::string &request) const;

  // ── 状态查询 ──
  std::string getState(const std::string &request) const;
};

} // namespace codepilot