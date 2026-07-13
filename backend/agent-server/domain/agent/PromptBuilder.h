#pragma once

#include "ExpertConfig.h"
#include "TaskContext.h"
#include <string>

namespace codepilot {

// ============================================================
// PromptBuilder — 模板驱动的 Prompt 构建器
// 按 Expert 的 JSON 配置动态注入标签协议 + 工具描述
//
// v2 变更：移除 buildContinue（不再有续接任务场景，
//   跨任务上下文通过 <read from="global"> 标签检索）
// ============================================================
class PromptBuilder {
public:
  // 构建首轮 prompt（Expert 上班时的初始上下文）
  static std::string buildInitial(const ExpertConfig &expert,
                                  const TaskContext &ctx);

  // 构建后续轮次 prompt（追加 LLM 输出和工具结果到 session）
  static std::string buildNextRound(const ExpertConfig &expert,
                                    const TaskContext &ctx,
                                    const std::string &sessionHistory,
                                    const std::string &lastOutput,
                                    int roundsLeft);

  // 读取模板文件内容
  static std::string loadTemplate(const std::string &templatePath);

private:
  // 替换占位符
  static std::string replaceAll(std::string tmpl, const std::string &from,
                                const std::string &to);

  // 生成标签协议说明（按 Expert 权限条件注入）
  static std::string buildTagProtocol(const ExpertConfig &expert);

  // 生成工具描述
  static std::string buildToolsDescription(const ExpertConfig &expert);

  // 生成输出提示
  static std::string buildOutputHint(const ExpertConfig &expert);
};

} // namespace codepilot