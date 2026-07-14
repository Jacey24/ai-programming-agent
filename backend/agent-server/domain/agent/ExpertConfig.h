#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace codepilot {

using json = nlohmann::json;

// ============================================================
// RouteRule — Expert 的路由规则
// ============================================================
struct RouteRule {
  enum ConditionType {
    TagExists,     // 检查 Expert 的最后输出中是否存在某标签
    TagValueMatch, // 检查某标签的内容是否匹配
    PlanState,     // 检查 plan 状态
    Default,       // 兜底路由（优先级最低）
  };

  ConditionType conditionType = Default;
  std::string conditionValue; // 标签名 / 匹配值 / plan 状态值
  std::string routeTo;        // 下一个 Expert 名（或内置目标 _done）
  int priority = 0;

  static ConditionType parseConditionType(const std::string &s) {
    if (s == "tag_exists")
      return TagExists;
    if (s == "tag_value_match")
      return TagValueMatch;
    if (s == "plan_state")
      return PlanState;
    if (s == "default")
      return Default;
    return Default;
  }

  static RouteRule fromJson(const json &j) {
    RouteRule r;
    r.conditionType = parseConditionType(j.value("type", "default"));
    r.conditionValue = j.value("value", "");
    r.routeTo = j.value("route_to", "");
    r.priority = j.value("priority", 0);
    return r;
  }
};

// ============================================================
// ExpertConfig — Expert JSON 配置的完整反序列化结构
// ============================================================
struct ExpertConfig {
  // 基本标识
  std::string name;
  std::string description;
  std::string promptTemplate; // 模板文件路径

  // 入口与隔离
  bool isEntry = false;
  bool contextIsolation = false;

  // 上下文模板（占位符：{role} {goal} {plan} {summary} {tools_desc}
  // {tag_protocol} {rounds_left} {session}）
  std::string contextTemplate;

  // ── 应用层 ──
  std::vector<std::string> visibleTools;

  // ── 系统层权限 ──
  bool canModifyPlan = true;
  bool canWriteSummary = false;
  bool readGlobalActively = false;
  int maxGlobalRounds = 0;

  // ── 路由 ──
  std::vector<RouteRule> nextRules;
  std::string onFailRoute;

  // ── LLM 配置（per-Expert，可选，未填时降级全局默认）──
  std::string llmProvider;      // deepseek / doubao / openai / ...
  std::string llmModel;         // deepseek-chat / doubao-pro-32k / ...
  int llmTimeout = 0;           // 0 = 使用全局默认
  double llmTemperature = -1.0; // -1 = 使用全局默认

  // ── 双检 ──
  bool doubleCheck = false;

  // ── 容量限制 ──
  int maxInternalRounds = 5;
  int toolTimeoutSeconds = 60;

  // ── JSON 反序列化 ──
  static ExpertConfig fromJson(const json &j) {
    ExpertConfig c;
    c.name = j.value("name", "");
    c.description = j.value("description", "");
    c.promptTemplate = j.value("prompt_template", "");
    c.isEntry = j.value("is_entry", false);
    c.contextIsolation = j.value("context_isolation", false);
    c.contextTemplate =
        j.value("context_template", "{role}\n{goal}\n{plan}\n{summary}\n{tag_"
                                    "protocol}\n{rounds_left}\n{session}");

    // 应用层
    if (j.contains("visible_tools") && j["visible_tools"].is_array()) {
      for (const auto &t : j["visible_tools"]) {
        c.visibleTools.push_back(t.get<std::string>());
      }
    }

    // 系统层
    c.canModifyPlan = j.value("can_modify_plan", true);
    c.canWriteSummary = j.value("can_write_summary", false);
    c.readGlobalActively = j.value("read_global_actively", false);
    c.maxGlobalRounds = j.value("max_global_rounds", 0);

    // 路由
    if (j.contains("next_rules") && j["next_rules"].is_array()) {
      for (const auto &r : j["next_rules"]) {
        c.nextRules.push_back(RouteRule::fromJson(r));
      }
    }
    c.onFailRoute = j.value("on_fail", "");

    // LLM 配置（支持带下划线前缀的旧字段名兼容）
    c.llmProvider = j.value("llm_provider", j.value("_llm_provider", ""));
    c.llmModel = j.value("llm_model", j.value("_llm_model", ""));
    c.llmTimeout = j.value("llm_timeout", j.value("_llm_timeout", 0));
    c.llmTemperature =
        j.value("llm_temperature", j.value("_llm_temperature", -1.0));

    // 双检
    c.doubleCheck = j.value("double_check", false);

    // 容量
    c.maxInternalRounds = j.value("max_internal_rounds", 5);
    c.toolTimeoutSeconds = j.value("tool_timeout_seconds", 60);

    return c;
  }
};

} // namespace codepilot