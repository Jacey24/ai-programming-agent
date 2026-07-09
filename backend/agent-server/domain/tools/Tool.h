#pragma once

#include <functional>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using json = nlohmann::json;

namespace codepilot {

// ============================================================
// 风险等级枚举
// 序列化时映射为小写字符串：safe, medium, dangerous, blocked
// ============================================================
enum class RiskLevel { Safe, Medium, Dangerous, Blocked };

inline std::string riskLevelToString(RiskLevel level) {
  switch (level) {
  case RiskLevel::Safe:
    return "safe";
  case RiskLevel::Medium:
    return "medium";
  case RiskLevel::Dangerous:
    return "dangerous";
  case RiskLevel::Blocked:
    return "blocked";
  default:
    return "unknown";
  }
}

inline RiskLevel riskLevelFromString(const std::string &s) {
  if (s == "safe")
    return RiskLevel::Safe;
  if (s == "medium")
    return RiskLevel::Medium;
  if (s == "dangerous")
    return RiskLevel::Dangerous;
  if (s == "blocked")
    return RiskLevel::Blocked;
  return RiskLevel::Medium;
}

// ============================================================
// 工具调用参数校验结果
// ============================================================
struct ValidationError {
  std::string field;
  std::string message;
};

// ============================================================
// 工具执行结果
// 与整体架构说明.md 9.3 节完全对齐
// ============================================================
struct ToolResult {
  bool success{false};
  std::string output;
  std::string error;
  json metadata; // 支持任意 JSON 结构，如 {"exit_code": 1, "duration_ms": 3400}
  int exitCode{0};

  static ToolResult Ok(const std::string &output = "") {
    ToolResult r;
    r.success = true;
    r.output = output;
    r.metadata = json::object();
    return r;
  }

  static ToolResult Err(const std::string &error, int exitCode = -1) {
    ToolResult r;
    r.success = false;
    r.error = error;
    r.exitCode = exitCode;
    r.metadata = json::object();
    return r;
  }

  std::string toString() const {
    std::ostringstream oss;
    oss << "ToolResult{success=" << (success ? "true" : "false") << ", output='"
        << output << "'"
        << ", error='" << error
        << "'"
           ", exitCode="
        << exitCode << "}";
    return oss.str();
  }
};

// ============================================================
// 工具执行上下文
// 说明：整体架构说明.md 9.2 节的接口定义较为简化，
// 实际实现增加了 ToolContext 参数，用于传递 task/workspace 上下文。
// ============================================================
struct ToolContext {
  std::string taskId;
  std::string workspaceId;
  std::string workspacePath;
  std::string sessionId;
  std::map<std::string, std::string> options;
};

// ============================================================
// 工具参数 Schema 描述（供 LLM 和 API 层使用）
// ============================================================
struct ToolParamSchema {
  std::string name;
  std::string type; // "string", "integer", "boolean", "object", "array"
  std::string description;
  bool required{false};
  std::string defaultValue;
};

struct ToolSchema {
  std::string name;
  std::string description;
  std::vector<ToolParamSchema> params;

  // 生成符合 OpenAI tool_call 格式的 JSON
  // 对应 GET /api/v1/tools/{name} 返回的 schema 字段
  json toOpenAISchema() const {
    json obj;
    obj["type"] = "function";

    json func;
    func["name"] = name;
    func["description"] = description;

    json parameters;
    parameters["type"] = "object";

    json properties = json::object();
    json required = json::array();

    for (const auto &p : params) {
      json prop;
      prop["type"] = p.type;
      prop["description"] = p.description;
      properties[p.name] = prop;

      if (p.required) {
        required.push_back(p.name);
      }
    }

    parameters["properties"] = properties;
    if (!required.empty()) {
      parameters["required"] = required;
    }

    func["parameters"] = parameters;
    obj["function"] = func;

    return obj;
  }

  // 生成简短的 summary（给渐进式提示用）
  json toSummary() const {
    json j;
    j["name"] = name;
    j["description"] = description;
    return j;
  }

  std::string toSummaryString() const {
    std::ostringstream oss;
    oss << "  - " << name << ": " << description;
    return oss.str();
  }
};

// ============================================================
// 预定义的工组分组名称（只做规范常量用，不强制校验）
// ============================================================
namespace ToolGroups {
constexpr const char *FILE = "file";
constexpr const char *GIT = "git";
constexpr const char *SHELL = "shell";
constexpr const char *WEB = "web";
constexpr const char *SKILL = "skill";
constexpr const char *MCP = "mcp";
constexpr const char *MISC = "misc";
} // namespace ToolGroups

// ============================================================
// Tool 抽象基类（所有工具的统一接口）
// 对齐整体架构说明.md 第9.2节，并扩展：
// - riskLevel() 方法用于权限判断
// - summary() / detail() 用于渐进式提示
// - validate() 用于执行前参数校验
// - group() 用于按功能域分组
// ============================================================
class Tool {
public:
  virtual ~Tool() = default;

  // --- 工具标识 ---
  virtual std::string name() const = 0;
  virtual std::string description() const = 0;

  // --- 工具分组（功能域） ---
  // 返回预定义分组名：file / git / shell / web / skill / mcp
  // 默认实现返回 "misc"，单个工具可以重写
  virtual std::string group() const { return ToolGroups::MISC; }

  // --- 元数据（供 LLM schema 生成和 API 展示） ---
  virtual ToolSchema schema() const = 0;

  // --- 风险等级（参数相关，调用时动态判断） ---
  virtual RiskLevel riskLevel(const json &arguments) const = 0;

  // --- 执行入口 ---
  // 参数 context: 工具执行上下文（task/workspace/session 信息）
  // 参数 arguments: 工具参数，符合 schema() 定义
  virtual ToolResult execute(const ToolContext &context,
                             const json &arguments) = 0;

  // --- 参数校验 ---
  virtual std::vector<ValidationError> validate(const json &arguments) const {
    std::vector<ValidationError> errors;
    auto s = schema();
    for (const auto &param : s.params) {
      if (param.required) {
        if (arguments.is_null() || !arguments.contains(param.name)) {
          errors.push_back(
              {param.name, "missing required parameter: " + param.name});
        }
      }
    }
    return errors;
  }

  // --- 轻量摘要（渐进式提示词用） ---
  // 返回 {name, description}，不包含详细参数
  virtual json summary() const { return schema().toSummary(); }

  // --- 完整详情（含完整 schema） ---
  // 返回完整的 OpenAI tool_call 格式
  virtual json detail() const { return schema().toOpenAISchema(); }
};

} // namespace codepilot