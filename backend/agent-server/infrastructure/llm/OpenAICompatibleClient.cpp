#include "infrastructure/llm/OpenAICompatibleClient.h"

#include <cstdlib>
#include <exception>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <utility>

// CPPHTTPLIB_OPENSSL_SUPPORT is defined via CMake target_compile_definitions
#include <httplib.h>

#include "common/logging/Logger.h"

using json = nlohmann::json;

namespace codepilot {
namespace {

// ── UTF-8 清洗：将字符串中所有非法 UTF-8 字节替换为 U+FFFD ──
std::string sanitizeUtf8(const std::string &input) {
  std::string result;
  result.reserve(input.size());
  for (size_t i = 0; i < input.size();) {
    unsigned char c = static_cast<unsigned char>(input[i]);
    size_t bytes = 0;
    if (c < 0x80) {
      bytes = 1;
    } else if ((c & 0xE0) == 0xC0) {
      bytes = 2;
    } else if ((c & 0xF0) == 0xE0) {
      bytes = 3;
    } else if ((c & 0xF8) == 0xF0) {
      bytes = 4;
    } else {
      // 非法起始字节 → 替换为 U+FFFD (3 bytes)
      result += "\xEF\xBF\xBD";
      ++i;
      continue;
    }
    if (i + bytes > input.size()) {
      result += "\xEF\xBF\xBD";
      break;
    }
    bool valid = true;
    for (size_t j = 1; j < bytes; ++j) {
      if ((static_cast<unsigned char>(input[i + j]) & 0xC0) != 0x80) {
        valid = false;
        break;
      }
    }
    if (valid) {
      result.append(input, i, bytes);
      i += bytes;
    } else {
      result += "\xEF\xBF\xBD";
      ++i;
    }
  }
  return result;
}

// ── 文件 I/O 工具 (保留，供 loadConfig 使用) ──

std::string readTextFile(const std::string &path) {
  std::ifstream in(path, std::ios::binary);
  if (!in.is_open()) {
    return "";
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

std::string trimTrailingSlash(std::string value) {
  while (!value.empty() && value.back() == '/') {
    value.pop_back();
  }
  return value;
}

std::string trimWhitespace(const std::string &value) {
  const auto begin = value.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) {
    return "";
  }
  const auto end = value.find_last_not_of(" \t\r\n");
  return value.substr(begin, end - begin + 1);
}

std::string localOverridePath(const std::string &path) {
  const std::string suffix = ".json";
  if (path.size() >= suffix.size() &&
      path.compare(path.size() - suffix.size(), suffix.size(), suffix) == 0) {
    return path.substr(0, path.size() - suffix.size()) + ".local.json";
  }
  return path + ".local";
}

// ── 配置解析 (保留) ──

void applyConfigJson(OpenAICompatibleConfig &config, const json &j) {
  config.baseUrl = j.value("base_url", config.baseUrl);
  config.model = j.value("model", config.model);
  config.apiKeyEnv = j.value("api_key_env", config.apiKeyEnv);
  config.apiKey = j.value("api_key", config.apiKey);
  config.apiKeyFile = j.value("api_key_file", config.apiKeyFile);
  config.timeoutSeconds = j.value("timeout_seconds", config.timeoutSeconds);
}

void applyConfigFile(OpenAICompatibleConfig &config, const std::string &path) {
  std::ifstream in(path);
  if (!in.is_open()) {
    return;
  }

  try {
    json j;
    in >> j;
    applyConfigJson(config, j);
  } catch (...) {
  }
}

// ── URL 解析 ──
// 将 "https://api.deepseek.com/v1" 拆分为 (scheme_host, path_prefix)
// scheme_host = "https://api.deepseek.com"
// path_prefix = "/v1"
struct ParsedUrl {
  std::string schemeHost; // "https://api.deepseek.com" (含 scheme)
  std::string pathPrefix; // "/v1" 或 ""
};

ParsedUrl parseBaseUrl(const std::string &baseUrl) {
  ParsedUrl result;
  result.schemeHost = baseUrl;
  result.pathPrefix = "";

  // 查找 "://" 之后的第一个 '/'
  auto schemePos = baseUrl.find("://");
  if (schemePos == std::string::npos) {
    return result; // 无法解析，返回原始 baseUrl
  }

  auto pathStart = schemePos + 3; // 跳过 "://"
  auto slashPos = baseUrl.find('/', pathStart);

  if (slashPos != std::string::npos) {
    result.schemeHost = baseUrl.substr(0, slashPos);
    result.pathPrefix = baseUrl.substr(slashPos);
  }

  return result;
}

} // namespace

// ── 构造 ──

OpenAICompatibleClient::OpenAICompatibleClient(OpenAICompatibleConfig config)
    : config_(std::move(config)) {}

// ── 静态工厂 / 检测 (保留) ──

OpenAICompatibleConfig
OpenAICompatibleClient::loadConfig(const std::string &path) {
  OpenAICompatibleConfig config;
  config.baseUrl = "https://api.example.com/v1";
  config.model = "gpt-4.1";
  config.apiKeyEnv = "LLM_API_KEY";

  applyConfigFile(config, path);
  applyConfigFile(config, localOverridePath(path));
  return config;
}

bool OpenAICompatibleClient::isConfigured(
    const OpenAICompatibleConfig &config) {
  if (config.baseUrl.empty() || config.model.empty()) {
    return false;
  }
  if (config.baseUrl.find("api.example.com") != std::string::npos) {
    return false;
  }
  return !resolveApiKey(config).empty();
}

std::string
OpenAICompatibleClient::resolveApiKey(const OpenAICompatibleConfig &config) {
  if (!config.apiKeyEnv.empty()) {
    const char *key = std::getenv(config.apiKeyEnv.c_str());
    if (key && *key) {
      return trimWhitespace(key);
    }
  }

  if (!config.apiKey.empty()) {
    return trimWhitespace(config.apiKey);
  }

  if (!config.apiKeyFile.empty()) {
    return trimWhitespace(readTextFile(config.apiKeyFile));
  }

  return "";
}

// ════════════════════════════════════════════════════════════
// chat() — 使用 httplib::Client（双平台统一，替换 curl）
// ════════════════════════════════════════════════════════════

LlmResponse OpenAICompatibleClient::chat(const LlmRequest &request) {
  LlmResponse result;

  LOG_INFO("[LLM DEBUG] chat() called, model={}, prompt_len={}",
           request.model.empty() ? config_.model : request.model,
           request.prompt.size());

  if (!isConfigured(config_)) {
    result.error = "LLM is not configured; using mock fallback";
    LOG_ERROR("[LLM DEBUG] chat() FAILED: isConfigured returned false. "
              "baseUrl={}, model={}",
              config_.baseUrl, config_.model);
    return result;
  }

  const std::string apiKey = resolveApiKey(config_);
  if (apiKey.empty()) {
    result.error = "LLM API key is empty; using mock fallback";
    LOG_ERROR("[LLM DEBUG] chat() FAILED: resolveApiKey returned empty. "
              "apiKeyEnv={}, apiKey_from_local={}",
              config_.apiKeyEnv, config_.apiKey.empty() ? "(empty)" : "(set)");
    return result;
  }
  LOG_INFO("[LLM DEBUG] API key resolved: len={}, apiKeyEnv={}", apiKey.size(),
           config_.apiKeyEnv);

  // ── 构建请求 JSON ──
  // ★ 清洗 prompt 中的非法 UTF-8 字节，防止 nlohmann::json 抛异常
  std::string cleanPrompt = sanitizeUtf8(request.prompt);
  json payload;
  payload["model"] = request.model.empty() ? config_.model : request.model;
  payload["messages"] =
      json::array({{{"role", "user"}, {"content", cleanPrompt}}});
  payload["temperature"] = 0.2;
  const std::string body = payload.dump();

  // ── 解析 baseUrl ──
  auto parsed = parseBaseUrl(config_.baseUrl);
  const std::string endpoint = parsed.pathPrefix + "/chat/completions";

  LOG_INFO("[LLM DEBUG] Parsed URL: schemeHost={}, endpoint={}",
           parsed.schemeHost, endpoint);

  // ── 创建 httplib::Client ──
  httplib::Client cli(parsed.schemeHost);

  const int timeout = request.timeoutSeconds > 0 ? request.timeoutSeconds
                                                 : config_.timeoutSeconds;
  LOG_INFO("[LLM DEBUG] Timeout: {}s (request={}, config={})", timeout,
           request.timeoutSeconds, config_.timeoutSeconds);
  cli.set_connection_timeout(timeout, 0);
  cli.set_read_timeout(timeout, 0);
  cli.set_write_timeout(timeout, 0);

  // 跟随重定向（最多 3 次）
  cli.set_follow_location(true);

  // ── 设置请求头 ──
  httplib::Headers headers{
      {"Content-Type", "application/json"},
      {"Authorization", "Bearer " + apiKey},
  };

  // ── 发送 POST 请求 ──
  LOG_INFO("[LLM DEBUG] Sending POST to {}...", endpoint);
  auto res = cli.Post(endpoint, headers, body, "application/json");
  LOG_INFO("[LLM DEBUG] POST returned, res_ptr={}", res ? "valid" : "null");

  if (!res) {
    result.error = "HTTP request to LLM failed: ";
    auto err = res.error();
    // httplib::Error 支持转换为字符串消息
    // 使用 httplib::to_string 获取错误描述
    result.error += httplib::to_string(err);
    LOG_ERROR("[LLM DEBUG] chat() FAILED: httplib error={}",
              httplib::to_string(err));
    return result;
  }

  const int status = res->status;
  const std::string &responseBody = res->body;
  LOG_INFO("[LLM DEBUG] HTTP status={}, body_len={}", status,
           responseBody.size());

  if (status < 200 || status >= 300) {
    result.error =
        "LLM API returned HTTP " + std::to_string(status) + ": " + responseBody;
    LOG_ERROR("[LLM DEBUG] chat() FAILED: HTTP status={}, body={}", status,
              responseBody.substr(0, 300));
    return result;
  }

  if (responseBody.empty()) {
    result.error = "LLM returned an empty response";
    LOG_ERROR("[LLM DEBUG] chat() FAILED: empty response body");
    return result;
  }

  // ── 解析响应 JSON ──
  try {
    const json parsedResp = json::parse(responseBody);
    if (parsedResp.contains("error")) {
      result.error = parsedResp["error"].dump();
      LOG_ERROR("[LLM DEBUG] chat() FAILED: API error in response: {}",
                result.error);
      return result;
    }

    const auto &choices = parsedResp.at("choices");
    if (!choices.is_array() || choices.empty()) {
      result.error = "LLM response has no choices";
      LOG_ERROR("[LLM DEBUG] chat() FAILED: no choices in response");
      return result;
    }

    result.content = choices.at(0).at("message").value("content", "");
    result.success = !result.content.empty();
    LOG_INFO("[LLM DEBUG] chat() SUCCESS: content_len={}",
             result.content.size());
    if (!result.success) {
      result.error = "LLM response content is empty";
      LOG_ERROR(
          "[LLM DEBUG] chat() WARN: content is empty despite valid response");
    }
  } catch (const std::exception &e) {
    result.error = std::string("failed to parse LLM response: ") + e.what();
    LOG_ERROR("[LLM DEBUG] chat() FAILED: JSON parse error: {}", e.what());
  }

  return result;
}

void OpenAICompatibleClient::chatStream(const LlmRequest &request,
                                        OnTokenCallback onToken) {
  if (!isConfigured(config_)) {
    return;
  }

  const std::string apiKey = resolveApiKey(config_);
  if (apiKey.empty()) {
    return;
  }

  json payload;
  payload["model"] = request.model.empty() ? config_.model : request.model;
  payload["messages"] = json::array(
      {{{"role", "user"}, {"content", sanitizeUtf8(request.prompt)}}});
  payload["temperature"] = 0.2;
  payload["stream"] = true;

  const auto parsed = parseBaseUrl(config_.baseUrl);
  const std::string endpoint = parsed.pathPrefix + "/chat/completions";
  httplib::Client cli(parsed.schemeHost);
  const int timeout = request.timeoutSeconds > 0 ? request.timeoutSeconds
                                                 : config_.timeoutSeconds;
  cli.set_connection_timeout(timeout, 0);
  cli.set_read_timeout(timeout, 0);
  cli.set_write_timeout(timeout, 0);
  cli.set_follow_location(true);

  const httplib::Headers headers{
      {"Content-Type", "application/json"},
      {"Authorization", "Bearer " + apiKey},
  };

  std::string pending;
  const auto processLine = [&onToken](std::string line) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line.rfind("data:", 0) != 0) {
      return;
    }
    std::string data = line.substr(5);
    if (!data.empty() && data.front() == ' ') {
      data.erase(data.begin());
    }
    if (data.empty() || data == "[DONE]") {
      return;
    }

    const json frame = json::parse(data, nullptr, false);
    if (frame.is_discarded() || !frame.contains("choices") ||
        !frame["choices"].is_array() || frame["choices"].empty()) {
      return;
    }
    const json &choice = frame["choices"][0];
    if (!choice.is_object() || !choice.contains("delta") ||
        !choice["delta"].is_object()) {
      return;
    }
    const json &delta = choice["delta"];
    if (delta.contains("content") && delta["content"].is_string()) {
      const std::string content = delta["content"].get<std::string>();
      if (!content.empty()) {
        onToken(content);
      }
    }
  };

  auto response = cli.Post(
      endpoint, headers, payload.dump(), "application/json",
      [&pending, &processLine](const char *data, size_t length) {
        pending.append(data, length);
        std::size_t newline = 0;
        while ((newline = pending.find('\n')) != std::string::npos) {
          processLine(pending.substr(0, newline));
          pending.erase(0, newline + 1);
        }
        return true;
      });

  if (!pending.empty()) {
    processLine(std::move(pending));
  }
  if (!response) {
    LOG_ERROR("[LLM STREAM] request failed: {}",
              httplib::to_string(response.error()));
  } else if (response->status < 200 || response->status >= 300) {
    LOG_ERROR("[LLM STREAM] HTTP status={}", response->status);
  }
}

} // namespace codepilot
