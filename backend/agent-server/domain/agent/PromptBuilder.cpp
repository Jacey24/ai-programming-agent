#include "PromptBuilder.h"
#include "application/ToolSystem.h"
#include <fstream>
#include <sstream>

namespace codepilot {

std::string PromptBuilder::replaceAll(std::string tmpl, const std::string &from,
                                      const std::string &to) {
  size_t pos = 0;
  while ((pos = tmpl.find(from, pos)) != std::string::npos) {
    tmpl.replace(pos, from.size(), to);
    pos += to.size();
  }
  return tmpl;
}

std::string PromptBuilder::loadTemplate(const std::string &templatePath) {
  if (templatePath.empty())
    return "";
  std::ifstream file(templatePath);
  if (!file.is_open())
    return ""; // 返回空，buildInitial 会使用内联默认值
  std::stringstream buf;
  buf << file.rdbuf();
  return buf.str();
}

std::string PromptBuilder::buildTagProtocol(const ExpertConfig &expert) {
  std::string proto;

  // <cmd> — 仅在 visibleTools 非空时注入
  if (!expert.visibleTools.empty()) {
    proto += "## 工具调用标签\n";
    proto += "调用工具时使用 <cmd> 标签，格式：\n";
    proto += "  <cmd>工具名 {\"参数名\":\"参数值\",...}</cmd>\n";
    proto += "例如：\n";
    proto += "  <cmd>file.write {\"path\":\"sort.py\",\"content\":\"def "
             "sort():...\"}</cmd>\n";
    proto += "  <cmd>shell.run {\"command\":\"python test.py\"}</cmd>\n\n";
  }

  // <plan> — 仅在 canModifyPlan 时注入
  if (expert.canModifyPlan) {
    proto += "## 计划操作标签\n";
    proto += "你可以使用 <plan> 标签修改项目计划：\n";
    proto += "  <plan>\n";
    proto += "    <add priority=\"1\">步骤描述</add>      ← "
             "新增步骤（1=最高优先级）\n";
    proto += "    <complete index=\"0\"/>                ← "
             "标记步骤完成（0=第一个步骤）\n";
    proto += "    <fail index=\"1\" reason=\"原因\"/>      ← "
             "标记步骤失败并说明原因\n";
    proto += "    <status/>                             ← 请求当前计划状态\n";
    proto += "  </plan>\n\n";
  }

  // <write to="summary"> — 仅在 canWriteSummary 时注入
  if (expert.canWriteSummary) {
    proto += "## 摘要写入标签\n";
    proto += "你可以使用 <write to=\"summary\"> 将关键发现或汇总写入任务摘要，"
             "供后续阶段参考：\n";
    proto +=
        "  <write "
        "to=\"summary\">审核通过，代码质量良好，已通过全部测试</write>\n\n";
  }

  // <read from="global"> — 仅在 readGlobalActively 时注入
  if (expert.readGlobalActively) {
    proto += "## 全局上下文检索标签\n";
    proto +=
        "你可以使用 <read from=\"global\"> 从历史任务归档中检索相关信息。\n";
    if (expert.maxGlobalRounds > 0) {
      proto += "你最多有 " + std::to_string(expert.maxGlobalRounds) +
               " 次检索机会（每次消耗1轮）。\n";
    }
    proto += "  <read from=\"global\" max_items=\"5\"/>\n\n";
  }

  // <done> / <fail> — 始终注入
  proto += "## 任务完成标签\n";
  proto += "当你完成当前阶段任务时，必须使用以下标签之一结束：\n";
  proto += "  <done>完成总结</done>  ← 正常完成，将流转到下一阶段\n";
  proto += "  <fail>失败原因</fail>   ← 遇到无法解决的问题\n\n";

  // <ask> — 始终注入
  proto += "## 用户交互标签\n";
  proto += "如需征求用户意见，使用：\n";
  proto += "  <ask>需要确认的问题</ask>  ← 任务将暂停等待用户回复\n\n";

  return proto;
}

std::string PromptBuilder::buildToolsDescription(const ExpertConfig &expert) {
  if (expert.visibleTools.empty())
    return "";

  std::string desc = "## 可用工具\n";
  if (ToolSystem::getInstance().isInitialized()) {
    auto &registry = ToolSystem::getInstance().registry();
    for (const auto &toolName : expert.visibleTools) {
      auto *tool = registry.getTool(toolName);
      if (tool) {
        // 构建参数简述（一行内完成名+描述+参数列表）
        std::string paramsBrief;
        auto schema = tool->schema();
        for (size_t i = 0; i < schema.params.size(); ++i) {
          const auto &p = schema.params[i];
          if (i > 0)
            paramsBrief += ", ";
          paramsBrief += p.name;
          if (p.required)
            paramsBrief += "(必填)";
          else
            paramsBrief += "(可选)";
        }
        desc += "  - " + tool->name() + ": " + tool->description();
        if (!paramsBrief.empty()) {
          desc += " 参数: " + paramsBrief;
        }
        desc += "\n";
      } else {
        desc += "  - " + toolName + "（未注册）\n";
      }
    }
  } else {
    for (const auto &toolName : expert.visibleTools) {
      desc += "  - " + toolName + "\n";
    }
  }
  desc += "\n注意: 每步调用一次只能执行一个工具。如需批量操作，请使用 "
          "shell.run 执行对应的 Shell 命令。\n\n";
  return desc;
}

std::string PromptBuilder::buildOutputHint(const ExpertConfig &expert) {
  std::string hint;
  hint += "## 关键规则（请逐条遵守）\n\n";
  hint += "### 1. 工具执行结果检查\n";
  hint += "每轮操作后，请检查对话历史中 [system] 开头的行：\n";
  hint += "  - 看到 EXECUTION_ERROR → "
          "操作失败了，分析原因后修正方案，不要结束任务\n";
  hint +=
      "  - 看到 NOT_AVAILABLE 或 TOOL_NOT_FOUND → 工具不可用，换一个可用工具\n";
  hint += "  - 看到 BLOCKED → 操作被永久禁止，必须完全换成另一种方案\n";
  hint += "  - 看到 PERMISSION_DENIED 或 PERMISSION_EXPIRED → "
          "等待用户批准，或换不需要权限的方案\n";
  hint += "  - 所有操作都成功且目标达成 → 可以结束当前阶段\n\n";
  hint += "### 2. 阶段结束方式\n";
  hint += "必须使用 done 标签或 fail 标签来结束当前阶段：\n";
  hint += "  - 任务完成 → 输出 done 标签，标签内容是完成总结\n";
  hint += "  - 任务失败 → 输出 fail 标签，标签内容是失败原因\n\n";
  hint += "### 3. 禁止事项\n";
  hint += "  - 禁止在未检查工具执行结果前就直接输出 done 标签\n";
  hint += "  - 禁止在 done 标签内只写一个词就结束，必须包含清晰完整的总结\n";
  hint += "  - 禁止在期望输出标签的行之外出现任何 XML 尖括号\n\n";
  if (expert.canWriteSummary) {
    hint +=
        "### 4. 摘要写入\n"
        "完成后请使用 write to=\"summary\" 标签将关键结果写入任务摘要。\n\n";
  }
  if (expert.canModifyPlan) {
    hint += "### 5. 计划更新\n"
            "请使用 plan 标签更新计划状态后，再输出 done 或 fail 标签。\n\n";
  }
  return hint;
}

std::string PromptBuilder::buildInitial(const ExpertConfig &expert,
                                        const TaskContext &ctx) {
  // 1. 加载模板
  std::string tmpl = loadTemplate(expert.promptTemplate);

  // 2. 如果模板加载失败，使用默认内联模板
  if (tmpl.empty()) {
    tmpl = "{role}\n\n{goal}\n\n{plan}\n{summary}\n{tools_desc}"
           "{tag_protocol}\n{output_hint}\n{rounds_left}";
  }

  // 3. 构建静态 parts
  std::string tagProto = buildTagProtocol(expert);
  std::string toolsDesc = buildToolsDescription(expert);
  std::string outputHint = buildOutputHint(expert);

  // 4. 替换占位符
  tmpl = replaceAll(tmpl, "{role}", expert.description);
  tmpl = replaceAll(tmpl, "{goal}", ctx.goal);

  std::string planText = ctx.currentPlan.toPromptFragment();
  // 计划为空时，对 planner 追加引导
  if (ctx.currentPlan.steps.empty() && expert.canModifyPlan) {
    planText += "\n\n⚠️ 你还没有创建计划。请按以下规则处理：\n"
                "- 如果用户需要执行操作（创建文件、运行命令、修改代码等），"
                "请使用 <plan><add priority=\"1\">第一步描述</add><add "
                "priority=\"2\">第二步描述</add></plan> 创建执行计划\n"
                "- 如果用户只是提问或闲聊，直接使用 <done>你的回答</done>\n"
                "- 禁止只输出空的 <status/>，这没有任何意义\n";
  }
  tmpl = replaceAll(tmpl, "{plan}", planText);

  tmpl = replaceAll(tmpl, "{summary}",
                    ctx.summary.empty() ? "（任务刚开始，暂无摘要）"
                                        : ctx.summary);
  tmpl = replaceAll(tmpl, "{tools_desc}", toolsDesc);
  tmpl = replaceAll(tmpl, "{tag_protocol}", tagProto);
  tmpl = replaceAll(tmpl, "{output_hint}", outputHint);
  tmpl = replaceAll(tmpl, "{rounds_left}",
                    "剩余轮次: " + std::to_string(expert.maxInternalRounds));
  tmpl = replaceAll(tmpl, "{session}", ""); // 首轮无 session

  return tmpl;
}

std::string PromptBuilder::buildNextRound(const ExpertConfig &expert,
                                          const TaskContext &ctx,
                                          const std::string &sessionHistory,
                                          const std::string &lastOutput,
                                          int roundsLeft) {
  std::string prompt;

  // 1. 重新构建首轮基础（去掉 session 占位符，手动追加）
  prompt = buildInitial(expert, ctx);

  // 2. 去掉原始结尾的 {rounds_left} 和 {session} 残留，手动追加
  // 追加完整 session 历史（长度截断保护）
  prompt += "\n\n## 对话历史\n";
  const size_t maxHistoryLen = 8000;
  if (sessionHistory.size() > maxHistoryLen) {
    prompt += "[system] ...(更早的对话已省略)...\n";
    prompt += sessionHistory.substr(sessionHistory.size() - 4000);
  } else {
    prompt += sessionHistory;
  }

  // 3. ★ 修复重复注入：sessionHistory 末尾已包含 [assistant] lastOutput，
  //    不再重复追加（AgentLoop.cpp:526 在 buildNextRound 之前已写入）

  // 4. 更新轮次信息
  prompt += "\n剩余轮次: " + std::to_string(roundsLeft);

  // 5. 如果轮次紧张，追加警告
  if (roundsLeft <= 2) {
    prompt += "\n\n⚠️ 你只剩 " + std::to_string(roundsLeft) +
              " 轮机会。如果无法完成，请输出 fail 标签并说明原因。\n";
  }

  prompt +=
      "\n请继续操作。若完成或失败，使用 done 标签或 fail 标签结束当前阶段。\n";

  return prompt;
}

} // namespace codepilot