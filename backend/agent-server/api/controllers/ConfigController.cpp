#include "ConfigController.h"
#include "facade/LlmClientFacade.h"

#include <chrono>
#include <fstream>
#include <nlohmann/json.hpp>
#include <regex>
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

std::string trimWhitespace(const std::string &value) {
  const auto begin = value.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos)
    return "";
  const auto end = value.find_last_not_of(" \t\r\n");
  return value.substr(begin, end - begin + 1);
}

bool isMaskedSecret(const std::string &value) {
  const std::string key = trimWhitespace(value);
  if (key.empty())
    return false;
  bool onlyMask = true;
  for (unsigned char c : key) {
    if (c != '*') {
      onlyMask = false;
      break;
    }
  }
  return onlyMask || key.find("****") != std::string::npos ||
         key.find("••") != std::string::npos;
}

bool validProviderId(const std::string &id) {
  static const std::regex pattern("^[A-Za-z0-9_-]+$");
  return std::regex_match(id, pattern);
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

  // Reload even when the previous configuration had no available provider.
  // init() is intentionally idempotent and therefore cannot refresh an
  // already initialized facade.
  auto &llm = LlmClientFacade::getInstance();
  if (llm.isInitialized()) {
    if (!llm.reloadConfig(LLM_PATH)) {
      json body;
      body["success"] = false;
      body["error"]["code"] = "LLM_RELOAD_FAILED";
      body["error"]["message"] = "LLM 配置已写入，但热加载失败";
      return http_response(body.dump(), "500 Internal Server Error");
    }
  } else {
    llm.init(LLM_PATH);
  }

  json body;
  body["success"] = true;
  return http_response(body.dump());
}

std::string
ConfigController::testLlmConnection(const std::string &request) const {
  auto &llm = LlmClientFacade::getInstance();
  std::string raw = request_body(request);
  json j = json::parse(raw, nullptr, false);
  if (j.is_discarded() || !j.is_object()) {
    json body;
    body["success"] = false;
    body["error"]["code"] = "INVALID_REQUEST";
    body["error"]["message"] = "请求体必须是有效的 JSON 对象";
    return http_response(body.dump(), "400 Bad Request");
  }
  const std::string provider = trimWhitespace(j.value("id", ""));
  const std::string baseUrl = trimWhitespace(j.value("base_url", ""));
  const std::string model = trimWhitespace(j.value("model", ""));
  std::string apiKey = trimWhitespace(j.value("api_key", ""));
  const std::string prompt =
      j.value("prompt", "Hello, respond with 'OK' in JSON.");
  if (provider.empty() || !validProviderId(provider)) {
    json body;
    body["success"] = false;
    body["error"]["code"] = "INVALID_PROVIDER_ID";
    body["error"]["message"] = "Provider ID 为空或格式不受支持";
    return http_response(body.dump(), "400 Bad Request");
  }
  if (baseUrl.empty() || model.empty()) {
    json body;
    body["success"] = false;
    body["error"]["code"] = "INVALID_REQUEST";
    body["error"]["message"] = "Base URL 和 Model 不能为空";
    return http_response(body.dump(), "400 Bad Request");
  }
  if (isMaskedSecret(apiKey))
    apiKey.clear();

  const auto started = std::chrono::steady_clock::now();
  auto resp = llm.testConnection(provider, baseUrl, model, apiKey, prompt);
  const auto latency = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - started)
                           .count();

  json body;
  body["success"] = resp.success;
  if (resp.success) {
    body["data"]["success"] = true;
    body["data"]["model"] = model;
    body["data"]["latency_ms"] = latency;
    body["data"]["response"] = resp.content;
    body["data"]["response_length"] = resp.content.size();
    body["data"]["response_preview"] =
        resp.content.substr(0, std::min(resp.content.size(), size_t(200)));
    return http_response(body.dump());
  }

  if (resp.errorKind == LlmErrorKind::NotConfigured) {
    body["error"]["code"] = "API_KEY_REQUIRED";
    body["error"]["message"] = "API Key 为空，且该 Provider 没有已保存的 Key";
    return http_response(body.dump(), "400 Bad Request");
  }
  if (resp.errorKind == LlmErrorKind::Transport) {
    body["error"]["code"] = "LLM_NETWORK_ERROR";
  } else if (resp.errorKind == LlmErrorKind::InvalidResponse ||
             resp.errorKind == LlmErrorKind::EmptyResponse) {
    body["error"]["code"] = "LLM_INVALID_RESPONSE";
  } else if (resp.httpStatus == 401 || resp.httpStatus == 403) {
    body["error"]["code"] = "LLM_AUTH_FAILED";
  } else if (resp.httpStatus == 400 || resp.httpStatus == 404) {
    body["error"]["code"] = "LLM_MODEL_UNAVAILABLE";
  } else {
    body["error"]["code"] = "LLM_REQUEST_FAILED";
  }
  body["error"]["message"] = resp.error;
  body["error"]["external_http_status"] = resp.httpStatus;
  body["data"]["latency_ms"] = latency;
  return http_response(body.dump(), "502 Bad Gateway");
}

// ── llm.json provider 管理 ──
std::string ConfigController::listLlmProviders() const {
  json llm = readJsonFile(LLM_PATH);
  json local = readJsonFile(LLM_LOCAL_PATH);
  json providers = json::array();

  if (llm.contains("providers") && llm["providers"].is_object()) {
    for (auto it = llm["providers"].begin(); it != llm["providers"].end();
         ++it) {
      json p = it.value();
      p["id"] = it.key();
      const bool hasProviderKey =
          local.contains("providers") && local["providers"].is_object() &&
          local["providers"].contains(it.key()) &&
          local["providers"][it.key()].is_object() &&
          local["providers"][it.key()].contains("api_key") &&
          !trimWhitespace(
               local["providers"][it.key()].value("api_key", ""))
               .empty();
      const bool hasLegacyDefaultKey =
          it.key() == llm.value("default", "") &&
          !trimWhitespace(local.value("api_key", "")).empty();
      p["api_key_masked"] = hasProviderKey || hasLegacyDefaultKey;
      if (p["api_key_masked"].get<bool>())
        p["api_key"] = "••••••";
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
  if (j.is_discarded() || !j.is_object()) {
    json body;
    body["success"] = false;
    body["error"]["code"] = "INVALID_REQUEST";
    body["error"]["message"] = "id, base_url, model 字段必填";
    return http_response(body.dump(), "400 Bad Request");
  }

  const std::string id = trimWhitespace(j.value("id", ""));
  const std::string baseUrl = trimWhitespace(j.value("base_url", ""));
  const std::string model = trimWhitespace(j.value("model", ""));
  const std::string apiKey = trimWhitespace(j.value("api_key", ""));
  if (id.empty() || !validProviderId(id) || baseUrl.empty() || model.empty()) {
    json body;
    body["success"] = false;
    body["error"]["code"] = "INVALID_PROVIDER";
    body["error"]["message"] =
        "Provider ID、Base URL、Model 均不能为空，且 ID 只能包含字母、数字、-、_";
    return http_response(body.dump(), "400 Bad Request");
  }
  if (apiKey.empty() || isMaskedSecret(apiKey)) {
    json body;
    body["success"] = false;
    body["error"]["code"] = "API_KEY_REQUIRED";
    body["error"]["message"] = "新 Provider 必须提供非掩码 API Key";
    return http_response(body.dump(), "400 Bad Request");
  }

  json llm = readJsonFile(LLM_PATH);
  if (!llm.contains("providers") || !llm["providers"].is_object())
    llm["providers"] = json::object();

  if (llm["providers"].contains(id)) {
    json body;
    body["success"] = false;
    body["error"]["code"] = "CONFLICT";
    body["error"]["message"] = "Provider [" + id + "] 已存在";
    return http_response(body.dump(), "409 Conflict");
  }

  json provider;
  provider["name"] = j.value("name", id);
  provider["base_url"] = baseUrl;
  provider["model"] = model;
  provider["api_key_env"] = j.value("api_key_env", "");
  provider["timeout_seconds"] = j.value("timeout_seconds", 120);
  llm["providers"][id] = provider;
  json local = readJsonFile(LLM_LOCAL_PATH);
  if (!local.contains("providers") || !local["providers"].is_object())
    local["providers"] = json::object();
  local["providers"][id]["api_key"] = apiKey;

  if (!writeConfigFile(LLM_PATH, llm.dump(2)) ||
      !writeConfigFile(LLM_LOCAL_PATH, local.dump(2))) {
    json body;
    body["success"] = false;
    body["error"]["code"] = "IO_ERROR";
    body["error"]["message"] = "Provider 配置持久化失败";
    return http_response(body.dump(), "500 Internal Server Error");
  }
  auto &facade = LlmClientFacade::getInstance();
  if (facade.isInitialized() ? !facade.reloadConfig(LLM_PATH)
                             : (facade.init(LLM_PATH), false)) {
    json body;
    body["success"] = false;
    body["error"]["code"] = "LLM_RELOAD_FAILED";
    body["error"]["message"] = "Provider 已保存，但运行时热加载失败";
    return http_response(body.dump(), "500 Internal Server Error");
  }

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

  if (j.contains("id") && trimWhitespace(j.value("id", "")) != id) {
    json body;
    body["success"] = false;
    body["error"]["code"] = "PROVIDER_ID_IMMUTABLE";
    body["error"]["message"] = "Provider ID 不允许在编辑时修改";
    return http_response(body.dump(), "400 Bad Request");
  }
  const std::string baseUrl =
      trimWhitespace(j.value("base_url", llm["providers"][id].value("base_url", "")));
  const std::string model =
      trimWhitespace(j.value("model", llm["providers"][id].value("model", "")));
  if (baseUrl.empty() || model.empty()) {
    json body;
    body["success"] = false;
    body["error"]["code"] = "INVALID_PROVIDER";
    body["error"]["message"] = "Base URL 和 Model 不能为空";
    return http_response(body.dump(), "400 Bad Request");
  }
  llm["providers"][id]["base_url"] = baseUrl;
  llm["providers"][id]["model"] = model;
  for (const char *field : {"name", "api_key_env", "timeout_seconds"}) {
    if (j.contains(field))
      llm["providers"][id][field] = j[field];
  }

  json local = readJsonFile(LLM_LOCAL_PATH);
  const std::string apiKey = trimWhitespace(j.value("api_key", ""));
  if (!apiKey.empty() && !isMaskedSecret(apiKey)) {
    if (!local.contains("providers") || !local["providers"].is_object())
      local["providers"] = json::object();
    local["providers"][id]["api_key"] = apiKey;
  }
  if (!writeConfigFile(LLM_PATH, llm.dump(2)) ||
      !writeConfigFile(LLM_LOCAL_PATH, local.dump(2))) {
    json body;
    body["success"] = false;
    body["error"]["code"] = "IO_ERROR";
    body["error"]["message"] = "Provider 配置持久化失败";
    return http_response(body.dump(), "500 Internal Server Error");
  }
  auto &facade = LlmClientFacade::getInstance();
  if (facade.isInitialized() && !facade.reloadConfig(LLM_PATH)) {
    json body;
    body["success"] = false;
    body["error"]["code"] = "LLM_RELOAD_FAILED";
    body["error"]["message"] = "Provider 已保存，但运行时热加载失败";
    return http_response(body.dump(), "500 Internal Server Error");
  }

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
  if (llm.value("default", "") == id) {
    llm["default"] = llm["providers"].empty()
                         ? ""
                         : llm["providers"].begin().key();
  }
  json local = readJsonFile(LLM_LOCAL_PATH);
  if (local.contains("providers") && local["providers"].is_object())
    local["providers"].erase(id);
  if (!writeConfigFile(LLM_PATH, llm.dump(2)) ||
      !writeConfigFile(LLM_LOCAL_PATH, local.dump(2))) {
    json body;
    body["success"] = false;
    body["error"]["code"] = "IO_ERROR";
    body["error"]["message"] = "删除 Provider 配置失败";
    return http_response(body.dump(), "500 Internal Server Error");
  }
  auto &facade = LlmClientFacade::getInstance();
  if (facade.isInitialized() && !facade.reloadConfig(LLM_PATH)) {
    json body;
    body["success"] = false;
    body["error"]["code"] = "LLM_RELOAD_FAILED";
    body["error"]["message"] = "Provider 已删除，但运行时热加载失败";
    return http_response(body.dump(), "500 Internal Server Error");
  }

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
    const std::string key = trimWhitespace(j["api_key"].get<std::string>());
    const json llm = readJsonFile(LLM_PATH);
    const std::string defaultProvider = llm.value("default", "");
    if (!key.empty() && !isMaskedSecret(key) && !defaultProvider.empty()) {
      if (!existing.contains("providers") ||
          !existing["providers"].is_object()) {
        existing["providers"] = json::object();
      }
      existing["providers"][defaultProvider]["api_key"] = key;
      existing.erase("api_key");
    }
  }
  if (j.contains("providers") && j["providers"].is_object()) {
    if (!existing.contains("providers") || !existing["providers"].is_object()) {
      existing["providers"] = json::object();
    }
    for (auto &[name, p] : j["providers"].items()) {
      if (!p.is_object() || !p.contains("api_key") ||
          !p["api_key"].is_string()) {
        continue;
      }
      const std::string key = trimWhitespace(p["api_key"].get<std::string>());
      if (!key.empty() && !isMaskedSecret(key)) {
        existing["providers"][name]["api_key"] = key;
      }
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
  if (LlmClientFacade::getInstance().isInitialized() &&
      !LlmClientFacade::getInstance().reloadConfig(LLM_PATH)) {
    json body;
    body["success"] = false;
    body["error"]["code"] = "LLM_RELOAD_FAILED";
    body["error"]["message"] = "API Key 已保存，但运行时热加载失败";
    return http_response(body.dump(), "500 Internal Server Error");
  }

  json body;
  body["success"] = true;
  return http_response(body.dump());
}

} // namespace codepilot
