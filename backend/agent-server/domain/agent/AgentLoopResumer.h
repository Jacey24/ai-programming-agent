#pragma once

#include "AgentConfiguration.h"
#include "Plan.h"
#include "TaskContext.h"

#include <string>
#include <vector>

namespace codepilot {

// ============================================================
// TaskSnapshot — Pause 时保存的完整任务状态
//
// 包含 runExpertChain() 中所有循环变量和上下文，
// 可序列化到 DB 或 JSON，Resume 时完整恢复。
// ============================================================
struct TaskSnapshot {
  // ── 循环状态（AgentLoop.cpp 第 108-114 行）──
  std::string currentExpert; // 当前在哪个 Expert
  int expertSwitches = 0;    // 已切换次数
  bool firstRoundInExpert = true;
  int roundsLeft = 0;
  int globalReadsUsed = 0;

  // ── 任务上下文（第 50-90 行 ctx）──
  std::string taskId;
  std::string globalId;
  std::string workspaceId;
  std::string goal;
  std::string workspacePath;
  std::string summary;                  // ctx.summary
  Plan currentPlan;                     // ctx.currentPlan
  std::vector<std::string> planHistory; // ctx.planHistory

  // ── 对话历史 ──
  std::string sessionHistory; // LLM 对话日志

  // ── 元数据 ──
  std::string pauseReason;         // "user" | "permission" | "auto"
  std::string permissionRequestId; // 如果是权限暂停，存储 requestId
};

// ============================================================
// ResumeResult — prepareResume() 的返回结果
//
// 组员 A 只需要调用 prepareResume() 并根据此结果判断后续流程。
// ============================================================
struct ResumeResult {
  TaskSnapshot
      snapshot; // 修改后的 snapshot（sessionHistory/summary/currentExpert
                // 可能已更新）
  bool handled = false;     // _resumer 已完全处理完任务（例如无用户消息时）
  std::string finalOutput;  // handled==true 时的最终输出
  std::string errorMessage; // 错误信息（空表示成功）
};

// ============================================================
// ResumeUtil — Resume 黑盒工具（纯增量，零侵入 AgentLoop）
//
// 组员 A 使用方式（唯一的调用入口）：
//
//   auto result = ResumeUtil::prepareResume(snapshot, userMessage);
//   if (!result.errorMessage.empty()) { /* 错误处理 */ }
//   // 用 result.snapshot 恢复执行...
//
// 内部逻辑：
//   1. 如果有用户消息 → 构建 _resumer Expert，调 LLM 处理
//   2. 更新 sessionHistory / summary / currentExpert
//   3. 返回修改后的 snapshot
// ============================================================
class ResumeUtil {
public:
  // 唯一的公共方法：准备恢复
  // @param snapshot   暂停时保存的完整状态
  // @param userMessage 用户追加的消息（可为空）
  // @return ResumeResult（含修改后的 snapshot）
  static ResumeResult prepareResume(const TaskSnapshot &snapshot,
                                    const std::string &userMessage);

private:
  // 构建 _resumer Expert 的配置（代码硬编码，不依赖 config/experts.json）
  static ExpertConfig buildResumerExpert();

  // 调用 LLM 让 _resumer 处理用户消息
  static std::string callResumer(const ExpertConfig &resumer,
                                 const TaskSnapshot &snapshot,
                                 const std::string &userMessage);
};

} // namespace codepilot