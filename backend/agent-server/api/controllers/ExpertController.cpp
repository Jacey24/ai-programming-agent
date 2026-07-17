#include "ExpertController.h"
#include "application/ToolSystem.h"
#include "domain/agent/AgentConfiguration.h"
#include "domain/agent/PromptBuilder.h"

#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>

namespace {

using json = nlohmann::json;

std::string http_response(const std::string &body,
                          const std::string &status = "200 OK") {
  std::ostringstream r;
  r << "HTTP/1.1 " << status << "\r\n"
    << "Content-Type: application/json; charset=utf-8\r\n"
    << "Access-Control-Allow-Origin: *\r\n"
    << "Access-Control-Allow-Methods: GET, POST, PUT, PATCH, DELETE, "
       "OPTIONS\r\n"
    << "Access-Control-Allow-Headers: Content-Type\r\n"
    << "Connection: close\r\n"
    << "Content-Length: " << body.size() << "\r\n\r\n"
    << body;
  return r.str();
}

std::string request_body(const std::string &request) {
  const std::size_t pos = request.find("\r\n\r\n");
  if (pos == std::string::npos)
    return "";
  return request.substr(pos + 4);
}

std::string extract_path_segment(const std::string &request,
                                 const std::string &prefix) {
  const std::size_t line_end = request.find("\r\n");
  const std::string line = request.substr(0, line_end);
  const std::size_t method_end = line.find(' ');
  if (method_end == std::string::npos)
    return "";
  const std::size_t path_start = method_end + 1;
  const std::size_t prefix_pos = line.find(prefix, path_start);
  if (prefix_pos == std::string::npos)
    return "";
  const std::size_t seg_start = prefix_pos + prefix.size();
  const std::size_t seg_end = line.find_first_of("? ", seg_start);
  if (seg_end == std::string::npos)
    return line.substr(seg_start);
  return line.substr(seg_start, seg_end - seg_start);
}

// 从 URL 段中提取 Expert 名称（/api/v1/experts/planner/tools → planner）
std::string extractExpertName(const std::string &segment) {
  auto slash = segment.find('/');
  if (slash != std::string::npos)
    return segment.substr(0, slash);
  return segment;
}

std::string extractToolName(const std::string &segment) {
  auto slash = segment.rfind('/');
  if (slash != std::string::npos && slash + 1 < segment.size())
    return segment.substr(slash + 1);
  return "";
}

int extractRouteIndex(const std::string &segment) {
  auto slash = segment.rfind('/');
  if (slash != std::string::npos && slash + 1 < segment.size()) {
    try {
      return std::stoi(segment.substr(slash + 1));
    } catch (...) {
    }
  }
  return -1;
}

// JSON 转 ExpertConfig 辅助（兼容前端发送的 JSON key）
codepilot::ExpertConfig expertFromJson(const json &j) {
  using namespace codepilot;
  ExpertConfig c;
  c.name = j.value("name", "");
  c.description = j.value("description", "");
  c.promptTemplate = j.value("prompt_template", "");
  c.isEntry = j.value("is_entry", false);
  c.contextIsolation = j.value("context_isolation", false);
  c.contextTemplate =
      j.value("context_template", "{role}\n{goal}\n{plan}\n{summary}\n{tag_"
                                  "protocol}\n{rounds_left}\n{session}");
  if (j.contains("visible_tools") && j["visible_tools"].is_array()) {
    for (const auto &t : j["visible_tools"])
      c.visibleTools.push_back(t.get<std::string>());
  }
  c.canModifyPlan = j.value("can_modify_plan", true);
  c.canWriteSummary = j.value("can_write_summary", false);
  c.readGlobalActively = j.value("read_global_actively", false);
  c.maxGlobalRounds = j.value("max_global_rounds", 0);
  if (j.contains("next_rules") && j["next_rules"].is_array()) {
    for (const auto &r : j["next_rules"])
      c.nextRules.push_back(RouteRule::fromJson(r));
  }
  c.onFailRoute = j.value("on_fail", "");
  c.maxInternalRounds = j.value("max_internal_rounds", 5);
  c.toolTimeoutSeconds = j.value("tool_timeout_seconds", 60);
  c.llmProvider = j.value("llm_provider", "");
  c.llmModel = j.value("llm_model", "");
  c.llmTimeout = j.value("llm_timeout", 0);
  c.llmTemperature = j.value("llm_temperature", -1.0);
  return c;
}

// 构建 Expert 节点 JSON
json buildNodeJson(const codepilot::ExpertConfig &e, const json &positions) {
  json node;
  node["id"] = e.name;
  node["label"] = e.name;
  node["description"] = e.description;
  node["is_entry"] = e.isEntry;
  node["is_exit"] = false;
  node["context_isolation"] = e.contextIsolation;
  node["visible_tools"] = e.visibleTools;
  node["permissions"]["can_modify_plan"] = e.canModifyPlan;
  node["permissions"]["can_write_summary"] = e.canWriteSummary;
  node["permissions"]["read_global_actively"] = e.readGlobalActively;
  node["llm"]["provider"] = e.llmProvider;
  node["llm"]["model"] = e.llmModel;
  node["llm"]["temperature"] = e.llmTemperature;
  node["llm"]["timeout"] = e.llmTimeout;
  node["limits"]["max_internal_rounds"] = e.maxInternalRounds;
  node["limits"]["tool_timeout_seconds"] = e.toolTimeoutSeconds;
  node["context_template"] = e.contextTemplate;
  node["on_fail"] = e.onFailRoute;
  if (positions.contains(e.name))
    node["position"] = positions[e.name];
  return node;
}

// 构建 allExperts → graph 结构
json buildGraph() {
  auto &cfg = codepilot::AgentConfiguration::getInstance();
  auto experts = cfg.allExperts();

  // 读取位置信息（存储在 experts.json 的 _ui.positions 中）
  json positions;
  try {
    json raw = json::parse(cfg.rawJson());
    if (raw.contains("_ui") && raw["_ui"].contains("positions"))
      positions = raw["_ui"]["positions"];
  } catch (...) {
  }

  json result;
  json nodes = json::array();
  json edges = json::array();

  int edgeId = 0;
  for (const auto &e : experts) {
    nodes.push_back(buildNodeJson(e, positions));

    // next_rules → edges
    for (const auto &r : e.nextRules) {
      json edge;
      edge["id"] = "edge_" + std::to_string(++edgeId);
      edge["source"] = e.name;
      edge["target"] = r.routeTo;
      std::string condType;
      switch (r.conditionType) {
      case codepilot::RouteRule::TagExists:
        condType = "tag_exists";
        edge["label"] = "输出 <" + r.conditionValue + ">";
        break;
      case codepilot::RouteRule::TagValueMatch:
        condType = "tag_value_match";
        edge["label"] = "匹配 " + r.conditionValue;
        break;
      case codepilot::RouteRule::PlanState:
        condType = "plan_state";
        edge["label"] = "计划状态: " + r.conditionValue;
        break;
      default:
        condType = "default";
        edge["label"] = "兜底路由";
        break;
      }
      edge["condition_type"] = condType;
      edge["condition_value"] = r.conditionValue;
      edge["priority"] = r.priority;
      edges.push_back(edge);
    }

    // on_fail → edge
    if (!e.onFailRoute.empty()) {
      json edge;
      edge["id"] = "edge_" + std::to_string(++edgeId);
      edge["source"] = e.name;
      edge["target"] = e.onFailRoute;
      edge["condition_type"] = "on_fail";
      edge["priority"] = 0;
      edge["label"] = "失败兜底";
      edges.push_back(edge);
    }
  }

  result["nodes"] = nodes;
  result["edges"] = edges;

  // 虚拟节点
  json virtuals = json::array();
  virtuals.push_back({{"id", "_done"}, {"label", "出口"}, {"type", "exit"}});
  virtuals.push_back({{"id", "_user"}, {"label", "入口"}, {"type", "entry"}});
  result["virtual_nodes"] = virtuals;

  return result;
}

// 静态辅助：检查 AgentConfiguration 状态
std::string checkReady() {
  if (!codepilot::AgentConfiguration::getInstance().isReady()) {
    json body;
    body["success"] = false;
    body["error"]["code"] = "CONFIG_ERROR";
    body["error"]["message"] = "Expert 配置未加载";
    return http_response(body.dump(), "500 Internal Server Error");
  }
  return "";
}

} // namespace

namespace codepilot {

std::string ExpertController::listExperts() const {
  auto err = checkReady();
  if (!err.empty())
    return err;

  auto &cfg = AgentConfiguration::getInstance();
  auto names = cfg.listExpertNames();

  json items = json::array();
  for (const auto &name : names) {
    const auto *e = cfg.getExpert(name);
    if (e) {
      json item;
      item["name"] = e->name;
      item["description"] = e->description;
      item["is_entry"] = e->isEntry;
      item["visible_tools_count"] = e->visibleTools.size();
      item["routes_count"] = e->nextRules.size();
      items.push_back(item);
    }
  }

  json body;
  body["success"] = true;
  body["data"]["items"] = items;
  return http_response(body.dump());
}

std::string ExpertController::getExpert(const std::string &request) const {
  auto err = checkReady();
  if (!err.empty())
    return err;

  std::string name =
      extractExpertName(extract_path_segment(request, "/api/v1/experts/"));
  if (name.empty()) {
    json body;
    body["success"] = false;
    body["error"]["code"] = "INVALID_REQUEST";
    body["error"]["message"] = "expert name is required";
    return http_response(body.dump(), "400 Bad Request");
  }

  auto &cfg = AgentConfiguration::getInstance();
  const auto *e = cfg.getExpert(name);
  if (!e) {
    json body;
    body["success"] = false;
    body["error"]["code"] = "NOT_FOUND";
    body["error"]["message"] = "Expert [" + name + "] 未找到";
    return http_response(body.dump(), "404 Not Found");
  }

  json positions;
  try {
    json raw = json::parse(cfg.rawJson());
    if (raw.contains("_ui") && raw["_ui"].contains("positions"))
      positions = raw["_ui"]["positions"];
  } catch (...) {
  }

  json body;
  body["success"] = true;
  body["data"] = buildNodeJson(*e, positions);
  return http_response(body.dump());
}

std::string ExpertController::getGraph(const std::string & /*request*/) const {
  auto err = checkReady();
  if (!err.empty())
    return err;

  json body;
  body["success"] = true;
  body["data"] = buildGraph();
  return http_response(body.dump());
}

std::string ExpertController::exportConfig() const {
  auto err = checkReady();
  if (!err.empty())
    return err;

  auto &cfg = AgentConfiguration::getInstance();
  json body;
  body["success"] = true;
  body["data"]["config"] = cfg.exportJson();
  body["data"]["config_path"] = cfg.configPath();
  return http_response(body.dump());
}

// ── CRUD ──

std::string ExpertController::createExpert(const std::string &request) const {
  auto err = checkReady();
  if (!err.empty())
    return err;

  std::string raw = request_body(request);
  json j = json::parse(raw, nullptr, false);
  if (j.is_discarded() || !j.is_object()) {
    json body;
    body["success"] = false;
    body["error"]["code"] = "INVALID_REQUEST";
    body["error"]["message"] = "请求体必须是有效的 JSON 对象";
    return http_response(body.dump(), "400 Bad Request");
  }

  ExpertConfig expert = expertFromJson(j);
  if (expert.name.empty()) {
    json body;
    body["success"] = false;
    body["error"]["code"] = "INVALID_REQUEST";
    body["error"]["message"] = "name 字段必填";
    return http_response(body.dump(), "400 Bad Request");
  }

  auto &cfg = AgentConfiguration::getInstance();
  if (!cfg.addExpert(expert)) {
    json body;
    body["success"] = false;
    body["error"]["code"] = "CONFLICT";
    body["error"]["message"] = "Expert [" + expert.name + "] 已存在";
    return http_response(body.dump(), "409 Conflict");
  }

  cfg.saveToFile();

  json body;
  body["success"] = true;
  body["data"]["name"] = expert.name;
  return http_response(body.dump(), "201 Created");
}

std::string ExpertController::updateExpert(const std::string &request) const {
  auto err = checkReady();
  if (!err.empty())
    return err;

  std::string name =
      extractExpertName(extract_path_segment(request, "/api/v1/experts/"));
  if (name.empty()) {
    json body;
    body["success"] = false;
    body["error"]["code"] = "INVALID_REQUEST";
    body["error"]["message"] = "expert name is required";
    return http_response(body.dump(), "400 Bad Request");
  }

  std::string raw = request_body(request);
  json j = json::parse(raw, nullptr, false);
  if (j.is_discarded() || !j.is_object()) {
    json body;
    body["success"] = false;
    body["error"]["code"] = "INVALID_REQUEST";
    body["error"]["message"] = "请求体必须是有效的 JSON 对象";
    return http_response(body.dump(), "400 Bad Request");
  }

  ExpertConfig expert = expertFromJson(j);
  auto &cfg = AgentConfiguration::getInstance();
  if (!cfg.updateExpert(name, expert)) {
    json body;
    body["success"] = false;
    body["error"]["code"] = "NOT_FOUND";
    body["error"]["message"] = "Expert [" + name + "] 未找到";
    return http_response(body.dump(), "404 Not Found");
  }

  cfg.saveToFile();

  json body;
  body["success"] = true;
  body["data"]["name"] = name;
  return http_response(body.dump());
}

std::string ExpertController::patchExpert(const std::string &request) const {
  auto err = checkReady();
  if (!err.empty())
    return err;

  std::string name =
      extractExpertName(extract_path_segment(request, "/api/v1/experts/"));
  if (name.empty()) {
    json body;
    body["success"] = false;
    body["error"]["code"] = "INVALID_REQUEST";
    body["error"]["message"] = "expert name is required";
    return http_response(body.dump(), "400 Bad Request");
  }

  std::string raw = request_body(request);
  auto &cfg = AgentConfiguration::getInstance();
  if (!cfg.patchExpert(name, raw)) {
    json body;
    body["success"] = false;
    body["error"]["code"] = "NOT_FOUND";
    body["error"]["message"] = "Expert [" + name + "] 未找到或 patch 无效";
    return http_response(body.dump(), "404 Not Found");
  }

  cfg.saveToFile();

  json body;
  body["success"] = true;
  body["data"]["name"] = name;
  return http_response(body.dump());
}

std::string ExpertController::deleteExpert(const std::string &request) const {
  auto err = checkReady();
  if (!err.empty())
    return err;

  std::string name =
      extractExpertName(extract_path_segment(request, "/api/v1/experts/"));
  if (name.empty()) {
    json body;
    body["success"] = false;
    body["error"]["code"] = "INVALID_REQUEST";
    body["error"]["message"] = "expert name is required";
    return http_response(body.dump(), "400 Bad Request");
  }

  auto &cfg = AgentConfiguration::getInstance();
  if (!cfg.removeExpert(name)) {
    json body;
    body["success"] = false;
    body["error"]["code"] = "NOT_FOUND";
    body["error"]["message"] = "Expert [" + name + "] 未找到";
    return http_response(body.dump(), "404 Not Found");
  }

  cfg.saveToFile();

  json body;
  body["success"] = true;
  return http_response(body.dump());
}

// ── 子资源 ──

std::string ExpertController::setTools(const std::string &request) const {
  auto err = checkReady();
  if (!err.empty())
    return err;

  std::string segment = extract_path_segment(request, "/api/v1/experts/");
  std::string name = extractExpertName(segment);

  std::string raw = request_body(request);
  json j = json::parse(raw, nullptr, false);
  if (j.is_discarded() || !j.contains("tools") || !j["tools"].is_array()) {
    json body;
    body["success"] = false;
    body["error"]["code"] = "INVALID_REQUEST";
    body["error"]["message"] = "请求体需包含 tools 数组";
    return http_response(body.dump(), "400 Bad Request");
  }

  std::vector<std::string> tools;
  for (const auto &t : j["tools"])
    tools.push_back(t.get<std::string>());

  auto &cfg = AgentConfiguration::getInstance();
  if (!cfg.setExpertTools(name, tools)) {
    json body;
    body["success"] = false;
    body["error"]["code"] = "NOT_FOUND";
    body["error"]["message"] = "Expert [" + name + "] 未找到";
    return http_response(body.dump(), "404 Not Found");
  }

  cfg.saveToFile();

  json body;
  body["success"] = true;
  body["data"]["expert"] = name;
  body["data"]["tools"] = tools;
  return http_response(body.dump());
}

std::string ExpertController::addTool(const std::string &request) const {
  auto err = checkReady();
  if (!err.empty())
    return err;

  std::string segment = extract_path_segment(request, "/api/v1/experts/");
  std::string name = extractExpertName(segment);

  std::string raw = request_body(request);
  json j = json::parse(raw, nullptr, false);
  std::string toolName = j.value("tool_name", "");
  if (toolName.empty()) {
    json body;
    body["success"] = false;
    body["error"]["code"] = "INVALID_REQUEST";
    body["error"]["message"] = "tool_name is required";
    return http_response(body.dump(), "400 Bad Request");
  }

  auto &cfg = AgentConfiguration::getInstance();
  const auto *e = cfg.getExpert(name);
  if (!e) {
    json body;
    body["success"] = false;
    body["error"]["code"] = "NOT_FOUND";
    body["error"]["message"] = "Expert [" + name + "] 未找到";
    return http_response(body.dump(), "404 Not Found");
  }

  auto tools = e->visibleTools;
  tools.push_back(toolName);
  cfg.setExpertTools(name, tools);
  cfg.saveToFile();

  json body;
  body["success"] = true;
  body["data"]["expert"] = name;
  body["data"]["tools"] = tools;
  return http_response(body.dump());
}

std::string ExpertController::removeTool(const std::string &request) const {
  auto err = checkReady();
  if (!err.empty())
    return err;

  std::string fullSegment = extract_path_segment(request, "/api/v1/experts/");
  std::string toolName = extractToolName(fullSegment);
  // expert name = fullSegment 去掉 /tools/{toolName} 后缀
  std::string suffix = "/tools/" + toolName;
  std::string name;
  if (fullSegment.size() > suffix.size() &&
      fullSegment.compare(fullSegment.size() - suffix.size(), suffix.size(),
                          suffix) == 0) {
    name = fullSegment.substr(0, fullSegment.size() - suffix.size());
  }

  if (name.empty() || toolName.empty()) {
    json body;
    body["success"] = false;
    body["error"]["code"] = "INVALID_REQUEST";
    body["error"]["message"] = "invalid path";
    return http_response(body.dump(), "400 Bad Request");
  }

  auto &cfg = AgentConfiguration::getInstance();
  const auto *e = cfg.getExpert(name);
  if (!e) {
    json body;
    body["success"] = false;
    body["error"]["code"] = "NOT_FOUND";
    body["error"]["message"] = "Expert [" + name + "] 未找到";
    return http_response(body.dump(), "404 Not Found");
  }

  auto tools = e->visibleTools;
  tools.erase(std::remove(tools.begin(), tools.end(), toolName), tools.end());
  cfg.setExpertTools(name, tools);
  cfg.saveToFile();

  json body;
  body["success"] = true;
  body["data"]["expert"] = name;
  body["data"]["tools"] = tools;
  return http_response(body.dump());
}

std::string ExpertController::setRoutes(const std::string &request) const {
  auto err = checkReady();
  if (!err.empty())
    return err;

  std::string segment = extract_path_segment(request, "/api/v1/experts/");
  std::string name = extractExpertName(segment);

  std::string raw = request_body(request);
  json j = json::parse(raw, nullptr, false);
  if (j.is_discarded() || !j.contains("routes") || !j["routes"].is_array()) {
    json body;
    body["success"] = false;
    body["error"]["code"] = "INVALID_REQUEST";
    body["error"]["message"] = "请求体需包含 routes 数组";
    return http_response(body.dump(), "400 Bad Request");
  }

  std::vector<RouteRule> routes;
  for (const auto &r : j["routes"])
    routes.push_back(RouteRule::fromJson(r));

  auto &cfg = AgentConfiguration::getInstance();
  if (!cfg.setExpertRoutes(name, routes)) {
    json body;
    body["success"] = false;
    body["error"]["code"] = "NOT_FOUND";
    body["error"]["message"] = "Expert [" + name + "] 未找到";
    return http_response(body.dump(), "404 Not Found");
  }

  cfg.saveToFile();

  json body;
  body["success"] = true;
  body["data"]["expert"] = name;
  return http_response(body.dump());
}

std::string ExpertController::addRoute(const std::string &request) const {
  auto err = checkReady();
  if (!err.empty())
    return err;

  std::string segment = extract_path_segment(request, "/api/v1/experts/");
  std::string name = extractExpertName(segment);

  std::string raw = request_body(request);
  json j = json::parse(raw, nullptr, false);
  RouteRule rule = RouteRule::fromJson(j);
  if (rule.routeTo.empty()) {
    json body;
    body["success"] = false;
    body["error"]["code"] = "INVALID_REQUEST";
    body["error"]["message"] = "route_to is required";
    return http_response(body.dump(), "400 Bad Request");
  }

  auto &cfg = AgentConfiguration::getInstance();
  if (!cfg.addExpertRoute(name, rule)) {
    json body;
    body["success"] = false;
    body["error"]["code"] = "NOT_FOUND";
    body["error"]["message"] = "Expert [" + name + "] 未找到";
    return http_response(body.dump(), "404 Not Found");
  }

  cfg.saveToFile();

  json body;
  body["success"] = true;
  body["data"]["expert"] = name;
  return http_response(body.dump());
}

std::string ExpertController::removeRoute(const std::string &request) const {
  auto err = checkReady();
  if (!err.empty())
    return err;

  std::string fullSegment = extract_path_segment(request, "/api/v1/experts/");

  // 路径: experts/{name}/routes/{index}
  int routeIndex = extractRouteIndex(fullSegment);
  std::string suffix = "/routes/" + std::to_string(routeIndex);
  std::string name;
  if (fullSegment.size() > suffix.size() &&
      fullSegment.compare(fullSegment.size() - suffix.size(), suffix.size(),
                          suffix) == 0) {
    name = fullSegment.substr(0, fullSegment.size() - suffix.size());
  }

  if (name.empty() || routeIndex < 0) {
    json body;
    body["success"] = false;
    body["error"]["code"] = "INVALID_REQUEST";
    body["error"]["message"] = "invalid path";
    return http_response(body.dump(), "400 Bad Request");
  }

  auto &cfg = AgentConfiguration::getInstance();
  if (!cfg.removeExpertRoute(name, routeIndex)) {
    json body;
    body["success"] = false;
    body["error"]["code"] = "NOT_FOUND";
    body["error"]["message"] = "Expert [" + name + "] 或路由索引 " +
                               std::to_string(routeIndex) + " 未找到";
    return http_response(body.dump(), "404 Not Found");
  }

  cfg.saveToFile();

  json body;
  body["success"] = true;
  body["data"]["expert"] = name;
  return http_response(body.dump());
}

std::string ExpertController::setLlm(const std::string &request) const {
  auto err = checkReady();
  if (!err.empty())
    return err;

  std::string segment = extract_path_segment(request, "/api/v1/experts/");
  std::string name = extractExpertName(segment);

  std::string raw = request_body(request);
  json j = json::parse(raw, nullptr, false);
  std::string provider = j.value("provider", "");
  std::string model = j.value("model", "");
  int timeout = j.value("timeout", 0);
  double temperature = j.value("temperature", -1.0);

  auto &cfg = AgentConfiguration::getInstance();
  if (!cfg.setExpertLlm(name, provider, model, timeout, temperature)) {
    json body;
    body["success"] = false;
    body["error"]["code"] = "NOT_FOUND";
    body["error"]["message"] = "Expert [" + name + "] 未找到";
    return http_response(body.dump(), "404 Not Found");
  }

  cfg.saveToFile();

  json body;
  body["success"] = true;
  body["data"]["expert"] = name;
  return http_response(body.dump());
}

// ── 导入/验证 ──

std::string ExpertController::importConfig(const std::string &request) const {
  std::string raw = request_body(request);
  json j = json::parse(raw, nullptr, false);
  std::string configStr =
      j.is_object() && j.contains("config") ? j["config"].dump() : raw;

  // 先验证
  std::string validateError;
  if (!AgentConfiguration::validate(configStr, validateError)) {
    json body;
    body["success"] = false;
    body["error"]["code"] = "VALIDATION_ERROR";
    body["error"]["message"] = validateError;
    return http_response(body.dump(), "400 Bad Request");
  }

  auto &cfg = AgentConfiguration::getInstance();
  cfg.importJson(configStr);
  cfg.saveToFile();

  json body;
  body["success"] = true;
  body["data"]["experts_count"] = cfg.listExpertNames().size();
  return http_response(body.dump());
}

std::string ExpertController::validateConfig(const std::string &request) const {
  std::string raw = request_body(request);
  json j = json::parse(raw, nullptr, false);
  std::string configStr =
      j.is_object() && j.contains("config") ? j["config"].dump() : raw;

  std::string error;
  bool ok = AgentConfiguration::validate(configStr, error);

  json body;
  body["success"] = true;
  body["data"]["valid"] = ok;
  if (!ok)
    body["data"]["error"] = error;
  return http_response(body.dump());
}

// ── 提示词预览 ──

std::string ExpertController::previewPrompt(const std::string &request) const {
  auto err = checkReady();
  if (!err.empty())
    return err;

  std::string segment = extract_path_segment(request, "/api/v1/experts/");
  std::string name = extractExpertName(segment);

  auto &cfg = AgentConfiguration::getInstance();
  const auto *expert = cfg.getExpert(name);
  if (!expert) {
    json body;
    body["success"] = false;
    body["error"]["code"] = "NOT_FOUND";
    body["error"]["message"] = "Expert [" + name + "] 未找到";
    return http_response(body.dump(), "404 Not Found");
  }

  std::string raw = request_body(request);
  json j = json::parse(raw, nullptr, false);

  TaskContext ctx;
  ctx.taskId = "preview";
  ctx.goal = j.value("goal", "示例任务目标");
  ctx.summary = j.value("summary", "");

  if (j.contains("plan") && j["plan"].is_string()) {
    Plan::Step step;
    step.index = 1;
    step.description = j["plan"].get<std::string>();
    ctx.currentPlan.steps.push_back(step);
  }

  std::string prompt = PromptBuilder::buildInitial(*expert, ctx);

  json body;
  body["success"] = true;
  body["data"]["expert"] = name;
  body["data"]["prompt"] = prompt;
  body["data"]["prompt_length"] = prompt.size();
  return http_response(body.dump());
}

// ── 画布位置 ──

std::string ExpertController::savePositions(const std::string &request) const {
  auto err = checkReady();
  if (!err.empty())
    return err;

  std::string raw = request_body(request);
  json positions = json::parse(raw, nullptr, false);

  auto &cfg = AgentConfiguration::getInstance();
  try {
    json rawJson = json::parse(cfg.rawJson());
    if (!rawJson.contains("_ui"))
      rawJson["_ui"] = json::object();
    rawJson["_ui"]["positions"] = positions;

    // 通过 import 更新内存 + 保存
    cfg.importJson(rawJson.dump());
    cfg.saveToFile();
  } catch (...) {
    json body;
    body["success"] = false;
    body["error"]["code"] = "INTERNAL_ERROR";
    body["error"]["message"] = "位置保存失败";
    return http_response(body.dump(), "500 Internal Server Error");
  }

  json body;
  body["success"] = true;
  return http_response(body.dump());
}

std::string ExpertController::getPositions() const {
  auto &cfg = AgentConfiguration::getInstance();
  json positions;
  try {
    json rawJson = json::parse(cfg.rawJson());
    if (rawJson.contains("_ui") && rawJson["_ui"].contains("positions"))
      positions = rawJson["_ui"]["positions"];
  } catch (...) {
  }

  json body;
  body["success"] = true;
  body["data"] = positions;
  return http_response(body.dump());
}

// ── 全局 LLM 默认值 ──

std::string ExpertController::getGlobalLlmDefaults() const {
  auto &cfg = AgentConfiguration::getInstance();
  auto d = cfg.getGlobalLlmDefaults();

  json body;
  body["success"] = true;
  body["data"]["provider"] = d.provider;
  body["data"]["model"] = d.model;
  body["data"]["timeout"] = d.timeout;
  body["data"]["temperature"] = d.temperature;
  return http_response(body.dump());
}

std::string
ExpertController::setGlobalLlmDefaults(const std::string &request) const {
  std::string raw = request_body(request);
  json j = json::parse(raw, nullptr, false);

  AgentConfiguration::GlobalLlmDefaults d;
  d.provider = j.value("provider", "");
  d.model = j.value("model", "");
  d.timeout = j.value("timeout", 0);
  d.temperature = j.value("temperature", -1.0);

  auto &cfg = AgentConfiguration::getInstance();
  cfg.setGlobalLlmDefaults(d);
  cfg.saveToFile();

  json body;
  body["success"] = true;
  return http_response(body.dump());
}

} // namespace codepilot