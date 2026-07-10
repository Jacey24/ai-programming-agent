#include "infrastructure/llm/OpenAICompatibleClient.h"

#include <cstdlib>
#include <cstdio>
#include <exception>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <utility>

using json = nlohmann::json;

namespace codepilot {
namespace {

std::string readTextFile(const std::string &path) {
  std::ifstream in(path, std::ios::binary);
  if (!in.is_open()) {
    return "";
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

bool writeTextFile(const std::string &path, const std::string &text) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out.is_open()) {
    return false;
  }
  out << text;
  return true;
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

std::string shellQuote(const std::string &value) {
#ifdef _WIN32
  std::string quoted = "\"";
  for (char ch : value) {
    if (ch == '"') {
      quoted += "\\\"";
    } else {
      quoted += ch;
    }
  }
  quoted += "\"";
  return quoted;
#else
  std::string quoted = "'";
  for (char ch : value) {
    if (ch == '\'') {
      quoted += "'\\''";
    } else {
      quoted += ch;
    }
  }
  quoted += "'";
  return quoted;
#endif
}

std::string tempPath(const std::string &name) {
  const char *tmpDir = std::getenv("TMPDIR");
  if (!tmpDir || !*tmpDir) {
    tmpDir = std::getenv("TEMP");
  }
  if (!tmpDir || !*tmpDir) {
    tmpDir = ".";
  }

  std::string dir = tmpDir;
  const char sep =
#ifdef _WIN32
      '\\';
#else
      '/';
#endif
  if (!dir.empty() && dir.back() != '/' && dir.back() != '\\') {
    dir.push_back(sep);
  }
  return dir + "codepilot_" + name + "_" + std::to_string(std::rand()) + ".json";
}

} // namespace

OpenAICompatibleClient::OpenAICompatibleClient(OpenAICompatibleConfig config)
    : config_(std::move(config)) {}

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

std::string OpenAICompatibleClient::resolveApiKey(
    const OpenAICompatibleConfig &config) {
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

LlmResponse OpenAICompatibleClient::chat(const LlmRequest &request) {
  LlmResponse result;

  if (!isConfigured(config_)) {
    result.error = "LLM is not configured; using mock fallback";
    return result;
  }

  const std::string apiKey = resolveApiKey(config_);
  if (apiKey.empty()) {
    result.error = "LLM API key is empty; using mock fallback";
    return result;
  }
  const std::string endpoint = trimTrailingSlash(config_.baseUrl) + "/chat/completions";

  json payload;
  payload["model"] = request.model.empty() ? config_.model : request.model;
  payload["messages"] = json::array(
      {{{"role", "user"}, {"content", request.prompt}}});
  payload["temperature"] = 0.2;

  const auto requestPath = tempPath("llm_request");
  const auto responsePath = tempPath("llm_response");
  if (!writeTextFile(requestPath, payload.dump())) {
    result.error = "failed to write temporary LLM request";
    return result;
  }

  const int timeout =
      request.timeoutSeconds > 0 ? request.timeoutSeconds : config_.timeoutSeconds;
  std::ostringstream command;
  command << "curl -sS --max-time " << timeout
          << " -X POST " << shellQuote(endpoint)
          << " -H " << shellQuote("Content-Type: application/json")
          << " -H " << shellQuote("Authorization: Bearer " + apiKey)
          << " --data-binary @" << shellQuote(requestPath)
          << " -o " << shellQuote(responsePath);

  const int exitCode = std::system(command.str().c_str());
  std::remove(requestPath.c_str());
  if (exitCode != 0) {
    std::remove(responsePath.c_str());
    result.error = "curl failed while calling LLM";
    return result;
  }

  const std::string body = readTextFile(responsePath);
  std::remove(responsePath.c_str());
  if (body.empty()) {
    result.error = "LLM returned an empty response";
    return result;
  }

  try {
    const json parsed = json::parse(body);
    if (parsed.contains("error")) {
      result.error = parsed["error"].dump();
      return result;
    }

    const auto &choices = parsed.at("choices");
    if (!choices.is_array() || choices.empty()) {
      result.error = "LLM response has no choices";
      return result;
    }

    result.content = choices.at(0).at("message").value("content", "");
    result.success = !result.content.empty();
    if (!result.success) {
      result.error = "LLM response content is empty";
    }
  } catch (const std::exception &e) {
    result.error = std::string("failed to parse LLM response: ") + e.what();
  }

  return result;
}

} // namespace codepilot
