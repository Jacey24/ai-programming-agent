#include "AgentLoopResumer.h"
#include "MessageBus.h"
#include "PromptBuilder.h"
#include "RouteEngine.h"
#include "facade/LlmClientFacade.h"

namespace codepilot {

// ============================================================
// buildResumerExpert — 构建 _resumer Expert 配置
//
// 代码硬编码，不依赖 config/experts.json。
// _resumer 的职责：接收快照 + 用户消息，注入 sessionHistory，
// 输出 <done> 后由 RouteEngine 路由到下一 Expert。
// ============================================================
ExpertConfig ResumeUtil::buildResumerExpert() {
  ExpertConfig config;
  config.name = "_resumer";
  config.description =
      "你是任务恢复助手。用户暂停了一个正在执行的任务并追加了新的信息。\n"
      "你的职责是：\n"
      "1. 阅读当前的任务状态（计划、摘要、最近对话历史）\n"
      "2. 理解用户追加的消息\n"
      "3. 将用户的意图整合到任务上下文中\n"
      "4. 用自然语言输出当前状态摘要并标记 <done>\n\n"
      "输出格式：\n"
      "<write to=\"summary\">用户追加的上下文摘要 + 原有摘要</write>\n"
      "<done>已理解您的意图，任务将继续执行。当前任务状态：...[简短汇报]...</"
      "done>\n\n"
      "注意事项：\n"
      "- 不要执行任何工具，不要做任何实质性操作\n"
      "- 只需整合上下文并输出 done\n"
      "- summary 应包含新旧信息，帮助后续 Expert 理解全貌";

  config.isEntry = false;
  config.contextIsolation = false; // 需要看到 sessionHistory
  config.visibleTools = {};        // _resumer 不调用任何工具
  config.canModifyPlan = false;    // _resumer 不修改计划
  config.canWriteSummary = true;   // 但可以更新 summary
  config.readGlobalActively = false;

  // 路由：done → 回到原 Expert 的下一个路由目标
  config.nextRules = {{"type", "tag_exists", "value", "done", "route_to",
                       "_resume_original", "priority", 10}};
  config.onFail = "_done";

  config.maxInternalRounds = 1; // 只允许 1 轮
  config.toolTimeoutSeconds = 60;

  return config;
}

// ============================================================
// callResumer — 调用 LLM 让 _resumer 处理用户消息
// ============================================================
std::string ResumeUtil::callResumer(const ExpertConfig &resumer,
                                    const TaskSnapshot &snapshot,
                                    const std::string &userMessage) {
  // 构建临时 TaskContext（用于 PromptBuilder）
  TaskContext ctx;
  ctx.taskId = snapshot.taskId;
  ctx.globalId = snapshot.globalId;
  ctx.workspaceId = snapshot.workspaceId;
  ctx.workspacePath = snapshot.workspacePath;
  ctx.goal = snapshot.goal;
  ctx.summary = snapshot.summary;
  ctx.currentPlan = snapshot.currentPlan;

  // 构建 prompt：注入完整的任务快照 + 用户消息
  std::string prompt = PromptBuilder::buildInitial(resumer, ctx);
  prompt += "\n\n## 暂停前的对话历史\n";
  prompt += snapshot.sessionHistory;
  prompt += "\n\n## 用户追加的消息\n";
  prompt += userMessage;
  prompt += "\n\n请处理以上信息，更新 summary 并输出 <done>。\n";

  // 调用 LLM
  if (!LlmClientFacade::getInstance().isAvailable()) {
    // LLM 不可用：用户消息直接注入 sessionHistory，不做额外处理
    return "<done>LLM 不可用，用户消息已追加到上下文：" + userMessage +
           "</done>";
  }

  LlmResponse resp = LlmClientFacade::getInstance().chat(prompt, "", "", 60);
  if (!resp.success || resp.content.empty()) {
    return "<done>LLM 调用失败，用户消息已追加到上下文：" + userMessage +
           "</done>";
  }

  return resp.content;
}

// ============================================================
// prepareResume — 唯一的公共入口
//
// 组员 A 黑盒使用：传入快照和用户消息，获取修改后的快照。
// ============================================================
ResumeResult ResumeUtil::prepareResume(const TaskSnapshot &snapshot,
                                       const std::string &userMessage) {
  ResumeResult result;
  result.snapshot = snapshot;

  // ── 情况 1：无用户消息 ──
  // 直接恢复，不需要 _resumer 处理。
  // sessionHistory 保持不变，PromptBuilder::buildNextRound 会正常追加。
  if (userMessage.empty()) {
    result.handled = false;
    return result;
  }

  // ── 情况 2：有用户消息 ──
  // 构建 _resumer Expert，调用 LLM 处理。
  ExpertConfig resumer = buildResumerExpert();
  std::string resumerOutput = callResumer(resumer, snapshot, userMessage);

  // 将 _resumer 输出注入 sessionHistory
  result.snapshot.sessionHistory +=
      "\n[system] ⏸ 任务暂停" +
      (snapshot.pauseReason.empty()
           ? ""
           : "（原因：" + snapshot.pauseReason + "）") +
      "\n[user] " + userMessage + "\n[assistant] " + resumerOutput + "\n";

  // 解析 _resumer 输出中的 <write to="summary"> 标签
  TagCollection tags = MessageBus::parse(resumerOutput);
  auto writeTags = tags.get("write");
  if (!writeTags.empty()) {
    for (const auto &wt : writeTags) {
      if (wt.attributes.value("to", "") == "summary") {
        result.snapshot.summary = wt.content;
      }
    }
  }

  // 解析 _resumer 输出中的 done/fail 标签
  TagCollection finalTags = MessageBus::parse(resumerOutput);

  if (finalTags.has("done")) {
    // _resumer 成功处理：获取原 Expert 的路由
    auto &cfg = AgentConfiguration::getInstance();
    const ExpertConfig *current = cfg.getExpert(snapshot.currentExpert);
    if (current) {
      Plan plan = result.snapshot.currentPlan;
      std::string next = RouteEngine::resolve(*current, finalTags, plan);
      if (next == "_done") {
        // _resumer 认为任务已完成
        result.handled = true;
        result.finalOutput =
            MessageBus::extractTagContent(resumerOutput, "done");
        return result;
      }
      // 更新 currentExpert 为路由目标
      const ExpertConfig *nextExpert = cfg.getExpert(next);
      if (nextExpert) {
        result.snapshot.currentExpert = next;
      }
    }
  } else if (finalTags.has("fail")) {
    result.errorMessage = "_resumer 无法处理用户消息：" +
                          MessageBus::extractTagContent(resumerOutput, "fail");
    return result;
  }

  // 重置循环状态（_resumer 已消耗 1 轮）
  result.snapshot.firstRoundInExpert = true;
  result.snapshot.roundsLeft = (current != nullptr)
                                   ? current->maxInternalRounds
                                   : result.snapshot.roundsLeft;

  result.handled = false;
  return result;
}

} // namespace codepilot