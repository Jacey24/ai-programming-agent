#include "ConfigController.h"
#include "facade/LlmClientFacade.h"

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

// ── 文件读写工具 ──
std::string readConfigFile(const std::string &path) {
  std::ifstream f(path);
  if (!f.is_open())
    return "";
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

bool writeConfigFile(const std::string &path, const std::string &content) {
  std::ofstream f(path, std::ios::trunc);
  if (!f.is_open())
    return false;
  f << content;
  f.close();
  return true;
}

// ── JSON 序列化与反序列化辅助 ──
json readJsonFile(const std::string &path) {
  std::string raw = readConfigFile(path);
  if (raw.empty())
    return json::object();
  return json::parse(raw, nullptr, false);
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

const std::string CONFIG_DIR = "config/";
const std::string AGENT_PATH = CONFIG_DIR + "agent.json";
const std::string LLM_PATH = CONFIG_DIR + "llm.json";
const std::string LLM_LOCAL_PATH = CONFIG_DIR + "llm.local.json";
const std::string WORKSPACE_PATH = CONFIG_DIR + "workspace.json";
const std::string LOGGING_PATH = CONFIG_DIR + "logging.json";
const std::string TOOLS_PATH = CONFIG_DIR + "tools.json";

} // namespace

namespace codepilot {

// ── 全局配置合并视图 ──
std::string ConfigController::getConfig() const {
  json result;
  result["agent"] = readJsonFile(AGENT_PATH);
  result["llm"] = readJsonFile(LLM_PATH);
  result["workspace"] = readJsonFile(WORKSPACE_PATH);
  result["logging"] = readJsonFile(LOGGING_PATH);

  json body;
  body["success"] = true;
  body["data"] = result;
  return http_response(body.dump());
}

// ── agent.json ──
std::string ConfigController::getAgentConfig() const {
  json data = readJsonFile(AGENT_PATH);
  json body;
  body["success"] = true;
  body["data"] = data;
  return http_response(body.dump());
}

std::string ConfigController::setAgentConfig(const std::string &request) const {
  std::string raw = request_body(request);
  json j = json::parse(raw, nullptr, false);
  if (j.is_discarded() || !j.is_object()) {
    json body;
    body["success"] = false;
    body["error"]["code"] = "INVALID_REQUEST";
    body["error"]["message"] = "请求体必须是有效的 JSON 对象";
    return http_response(body.dump(), "400 Bad Request");
  }

  if (!writeConfigFile(AGENT_PATH, j.dump(2))) {
    json body;
    body["success"] = false;
    body["error"]["code"] = "IO_ERROR";
    body["error"]["message"] = "写入 agent.json 失败";
    return http_response(body.dump(), "500 Internal Server Error");
  }

  json body;
  body["success"] = true;
  return http_response(body.dump());
}

// ── llm.json ──
std::string ConfigController::getLlmConfig() const {
  json data = readJsonFile(LLM_PATH);
  json body;
  body["success"] = true;
  body["data"] = data;
  return http_response(body.dump());
}

std::string ConfigController::setLlmConfig(const std::string &request) const {
  std::string raw = request_body(request);
  json j = json::parse(raw, nullptr, false);
  if (j.is_discarded() || !j.is_object()) {
    json body;
    body["success"] = false;
    body["error"]["code"] = "INVALID_REQUEST";
    body["error"]["message"] = "请求体必须是有效的 JSON 对象";
    return http_response(body.dump(), "400 Bad Request");
  }

  if (!writeConfigFile(LLM_PATH, j.dump(2))) {
    json body;
    body["success"] = false;
    body["error"]["code"] = "IO_ERROR";
    body["error"]["message"] = "写入 llm.json 失败";
    return http_response(body.dump(), "500 Internal Server Error");
  }

  // 重新初始化 LlmClientFacade
  if (LlmClientFacade::getInstance().isAvailable()) {
    LlmClientFacade::getInstance().init(LLM_PATH);
  }

  json body;
  body["success"] = true;
  return http_response(body.dump());
}

std::string
ConfigController::testLlmConnection(const std::string &request) const {
  if (!LlmClientFacade::getInstance().isAvailable()) {
    json body;
    body["success"] = false;
    body["error"]["code"] = "LLM_NOT_AVAILABLE";
    body["error"]["message"] = "LLM 未初始化";
    return http_response(body.dump(), "503 Service Unavailable");
  }

  std::string raw = request_body(request);
  json j = json::parse(raw, nullptr, false);
  std::string prompt = j.value("prompt", "Hello, respond with 'OK' in JSON.");

  auto resp = LlmClientFacade::getInstance().chat(prompt);

  json body;
  body["success"] = resp.success;
  body["data"]["success"] = resp.success;
  body["data"]["error"] = resp.error;
  if (resp.success) {
    body["data"]["response_length"] = resp.content.size();
    body["data"]["response_preview"] =
        resp.content.substr(0, std::min(resp.content.size(), size_t(200)));
  }
  return http_response(body.dump());
}

// ── llm.json provider 管理 ──
std::string ConfigController::listLlmProviders() const {
  json llm = readJsonFile(LLM_PATH);
  json providers = json::array();

  if (llm.contains("providers") && llm["providers"].is_object()) {
    for (auto it = llm["providers"].begin(); it != llm["providers"].end();
         ++it) {
      json p = it.value();
      p["id"] = it.key();
      providers.push_back(p);
    }
  }

  json body;
  body["success"] = true;
  body["data"]["providers"] = providers;
  body["data"]["default"] = llm.value("default", "");
  return http_response(body.dump());
}

std::string ConfigController::addLlmProvider(const std::string &request) const {
  std::string raw = request_body(request);
  json j = json::parse(raw, nullptr, false);
  if (j.is_discarded() || !j.contains("id") || !j.contains("base_url")) {
    json body;
    body["success"] = false;
    body["error"]["code"] = "INVALID_REQUEST";
    body["error"]["message"] = "id, base_url, model 字段必填";
    return http_response(body.dump(), "400 Bad Request");
  }

  json llm = readJsonFile(LLM_PATH);
  std::string id = j["id"].get<std::string>();

  if (llm["providers"].contains(id)) {
    json body;
    body["success"] = false;
    body["error"]["code"] = "CONFLICT";
    body["error"]["message"] = "Provider [" + id + "] 已存在";
    return http_response(body.dump(), "409 Conflict");
  }

  json provider;
  provider["name"] = j.value("name", id);
  provider["base_url"] = j["base_url"];
  provider["model"] = j.value("model", "");
  provider["api_key_env"] = j.value("api_key_env", "");
  provider["timeout_seconds"] = j.value("timeout_seconds", 120);
  llm["providers"][id] = provider;

  writeConfigFile(LLM_PATH, llm.dump(2));

  json body;
  body["success"] = true;
  body["data"]["id"] = id;
  return http_response(body.dump(), "201 Created");
}

std::string
ConfigController::updateLlmProvider(const std::string &request) const {
  std::string id =
      extract_path_segment(request, "/api/v1/config/llm/providers/");

  std::string raw = request_body(request);
  json j = json::parse(raw, nullptr, false);
  if (j.is_discarded() || !j.is_object()) {
    json body;
    body["success"] = false;
    body["error"]["code"] = "INVALID_REQUEST";
    body["error"]["message"] = "请求体必须是有效的 JSON 对象";
    return http_response(body.dump(), "400 Bad Request");
  }

  json llm = readJsonFile(LLM_PATH);
  if (!llm["providers"].contains(id)) {
    json body;
    body["success"] = false;
    body["error"]["code"] = "NOT_FOUND";
    body["error"]["message"] = "Provider [" + id + "] 未找到";
    return http_response(body.dump(), "404 Not Found");
  }

  // 合并更新
  for (auto it = j.begin(); it != j.end(); ++it) {
    llm["providers"][id][it.key()] = it.value();
  }

  writeConfigFile(LLM_PATH, llm.dump(2));

  json body;
  body["success"] = true;
  body["data"]["id"] = id;
  return http_response(body.dump());
}

std::string
ConfigController::deleteLlmProvider(const std::string &request) const {
  std::string id =
      extract_path_segment(request, "/api/v1/config/llm/providers/");

  json llm = readJsonFile(LLM_PATH);
  if (!llm["providers"].contains(id)) {
    json body;
    body["success"] = false;
    body["error"]["code"] = "NOT_FOUND";
    body["error"]["message"] = "Provider [" + id + "] 未找到";
    return http_response(body.dump(), "404 Not Found");
  }

  llm["providers"].erase(id);
  writeConfigFile(LLM_PATH, llm.dump(2));

  json body;
  body["success"] = true;
  return http_response(body.dump());
}

// ── workspace.json ──
std::string ConfigController::getWorkspaceConfig() const {
  json data = readJsonFile(WORKSPACE_PATH);
  json body;
  body["success"] = true;
  body["data"] = data;
  return http_response(body.dump());
}

std::string
ConfigController::setWorkspaceConfig(const std::string &request) const {
  std::string raw = request_body(request);
  json j = json::parse(raw, nullptr, false);
  if (j.is_discarded() || !j.is_object()) {
    json body;
    body["success"] = false;
    body["error"]["code"] = "INVALID_REQUEST";
    body["error"]["message"] = "请求体必须是有效的 JSON 对象";
    return http_response(body.dump(), "400 Bad Request");
  }

  if (!writeConfigFile(WORKSPACE_PATH, j.dump(2))) {
    json body;
    body["success"] = false;
    body["error"]["code"] = "IO_ERROR";
    body["error"]["message"] = "写入 workspace.json 失败";
    return http_response(body.dump(), "500 Internal Server Error");
  }

  json body;
  body["success"] = true;
  return http_response(body.dump());
}

// ── logging.json ──
std::string ConfigController::getLoggingConfig() const {
  json data = readJsonFile(LOGGING_PATH);
  json body;
  body["success"] = true;
  body["data"] = data;
  return http_response(body.dump());
}

std::string
ConfigController::setLoggingConfig(const std::string &request) const {
  std::string raw = request_body(request);
  json j = json::parse(raw, nullptr, false);
  if (j.is_discarded() || !j.is_object()) {
    json body;
    body["success"] = false;
    body["error"]["code"] = "INVALID_REQUEST";
    body["error"]["message"] = "请求体必须是有效的 JSON 对象";
    return http_response(body.dump(), "400 Bad Request");
  }

  if (!writeConfigFile(LOGGING_PATH, j.dump(2))) {
    json body;
    body["success"] = false;
    body["error"]["code"] = "IO_ERROR";
    body["error"]["message"] = "写入 logging.json 失败";
    return http_response(body.dump(), "500 Internal Server Error");
  }

  json body;
  body["success"] = true;
  return http_response(body.dump());
}

// ── tools.json ──
std::string ConfigController::getToolsConfig() const {
  json data = readJsonFile(TOOLS_PATH);
  json body;
  body["success"] = true;
  body["data"] = data;
  return http_response(body.dump());
}

std::string ConfigController::setToolsConfig(const std::string &request) const {
  std::string raw = request_body(request);
  json j = json::parse(raw, nullptr, false);
  if (j.is_discarded() || !j.is_object()) {
    json body;
    body["success"] = false;
    body["error"]["code"] = "INVALID_REQUEST";
    body["error"]["message"] = "请求体必须是有效的 JSON 对象";
    return http_response(body.dump(), "400 Bad Request");
  }

  if (!writeConfigFile(TOOLS_PATH, j.dump(2))) {
    json body;
    body["success"] = false;
    body["error"]["code"] = "IO_ERROR";
    body["error"]["message"] = "写入 tools.json 失败";
    return http_response(body.dump(), "500 Internal Server Error");
  }

  json body;
  body["success"] = true;
  return http_response(body.dump());
}

// ── llm.local.json ──
std::string ConfigController::getLlmLocalConfig() const {
  json data = readJsonFile(LLM_LOCAL_PATH);

  // 安全脱敏：隐藏 api_key 中间部分
  if (data.contains("api_key") && data["api_key"].is_string()) {
    std::string key = data["api_key"].get<std::string>();
    if (key.size() > 8) {
      data["api_key"] = key.substr(0, 4) + "****" + key.substr(key.size() - 4);
    } else {
      data["api_key"] = "****";
    }
    data["api_key_masked"] = true;
  }
  if (data.contains("providers") && data["providers"].is_object()) {
    for (auto &[name, p] : data["providers"].items()) {
      if (p.contains("api_key") && p["api_key"].is_string()) {
        std::string key = p["api_key"].get<std::string>();
        if (key.size() > 8) {
          p["api_key"] = key.substr(0, 4) + "****" + key.substr(key.size() - 4);
        } else {
          p["api_key"] = "****";
        }
        p["api_key_masked"] = true;
      }
    }
  }

  json body;
  body["success"] = true;
  body["data"] = data;
  return http_response(body.dump());
}

std::string
ConfigController::setLlmLocalConfig(const std::string &request) const {
  std::string raw = request_body(request);
  json j = json::parse(raw, nullptr, false);
  if (j.is_discarded() || !j.is_object()) {
    json body;
    body["success"] = false;
    body["error"]["code"] = "INVALID_REQUEST";
    body["error"]["message"] = "请求体必须是有效的 JSON 对象";
    return http_response(body.dump(), "400 Bad Request");
  }

  // 与已有 llm.local.json 合并（保留未修改的 provider keys）
  json existing = readJsonFile(LLM_LOCAL_PATH);
  if (j.contains("api_key") && j["api_key"].is_string()) {
    existing["api_key"] = j["api_key"];
  }
  if (j.contains("providers") && j["providers"].is_object()) {
    if (!existing.contains("providers") || !existing["providers"].is_object()) {
      existing["providers"] = json::object();
    }
    for (auto &[name, p] : j["providers"].items()) {
      existing["providers"][name] = p;
    }
  }

  if (!writeConfigFile(LLM_LOCAL_PATH, existing.dump(2))) {
    json body;
    body["success"] = false;
    body["error"]["code"] = "IO_ERROR";
    body["error"]["message"] = "写入 llm.local.json 失败";
    return http_response(body.dump(), "500 Internal Server Error");
  }

  // 热加载 API Key
  if (LlmClientFacade::getInstance().isInitialized()) {
    LlmClientFacade::getInstance().reloadConfig();
  }

  json body;
  body["success"] = true;
  return http_response(body.dump());
}

} // namespace codepilot
