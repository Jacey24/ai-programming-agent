#include "facade/LlmClientFacade.h"

#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>

using json = nlohmann::json;

namespace codepilot {

// ============================================================
// 单例获取
// ============================================================
LlmClientFacade &LlmClientFacade::getInstance() {
  static LlmClientFacade instance;
  return instance;
}

// ============================================================
// 工具函数
// ============================================================
namespace {

std::string localOverridePath(const std::string &path) {
  const std::string suffix = ".json";
  if (path.size() >= suffix.size() &&
      path.compare(path.size() - suffix.size(), suffix.size(), suffix) == 0) {
    return path.substr(0, path.size() - suffix.size()) + ".local.json";
  }
  return path + ".local";
}

std::string readTextFile(const std::string &path) {
  std::ifstream in(path);
  if (!in.is_open()) {
    return "";
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

std::string trimWhitespace(const std::string &value) {
  const auto begin = value.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) {
    return "";
  }
  const auto end = value.find_last_not_of(" \t\r\n");
  return value.substr(begin, end - begin + 1);
}

} // namespace

// ============================================================
// init — 幂等初始化
// ============================================================
void LlmClientFacade::init(const std::string &configPath) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (initialized_) {
    return;
  }
  configPath_ = configPath;
  loadProvidersFromConfig(configPath);
  loadLocalOverrides(configPath);
  buildClients();
  initialized_ = true;
}

// ============================================================
// 从 llm.json 加载所有 Provider 配置
// ============================================================
void LlmClientFacade::loadProvidersFromConfig(const std::string &configPath) {
  configs_.clear();

  const std::string raw = readTextFile(configPath);
  if (raw.empty()) {
    return;
  }

  json config;
  try {
    config = json::parse(raw);
  } catch (const std::exception &) {
    return;
  }

  defaultProvider_ = config.value("default", "");

  if (!config.contains("providers") || !config["providers"].is_object()) {
    return;
  }

  for (auto it = config["providers"].begin(); it != config["providers"].end();
       ++it) {
    const std::string &key = it.key();
    const json &p = it.value();

    ProviderConfig pc;
    pc.key = key;
    pc.name = p.value("name", key);
    pc.baseUrl = p.value("base_url", "");
    pc.model = p.value("model", "");
    pc.apiKeyEnv = p.value("api_key_env", "");
    pc.timeoutSeconds = p.value("timeout_seconds", 120);

    configs_.push_back(pc);
  }
}

// ============================================================
// 从 llm.local.json 加载 API Key 覆盖
// ============================================================
void LlmClientFacade::loadLocalOverrides(const std::string &configPath) {
  const std::string localPath = localOverridePath(configPath);
  const std::string raw = readTextFile(localPath);
  if (raw.empty()) {
    return;
  }

  json localConfig;
  try {
    localConfig = json::parse(raw);
  } catch (const std::exception &) {
    return;
  }

  // 格式 1：{ "api_key": "sk-xxx" } → 覆盖所有 provider
  if (localConfig.contains("api_key") && localConfig["api_key"].is_string()) {
    const std::string globalKey =
        trimWhitespace(localConfig["api_key"].get<std::string>());
    for (auto &pc : configs_) {
      pc.apiKey = globalKey;
    }
    return;
  }

  // 格式 2：{ "providers": { "doubao": {"api_key": "sk-xxx"} } }
  if (localConfig.contains("providers") &&
      localConfig["providers"].is_object()) {
    for (auto &pc : configs_) {
      if (localConfig["providers"].contains(pc.key)) {
        const json &override = localConfig["providers"][pc.key];
        if (override.contains("api_key") && override["api_key"].is_string()) {
          pc.apiKey = trimWhitespace(override["api_key"].get<std::string>());
        }
      }
    }
  }
}

// ============================================================
// 构建 Client 实例
// ============================================================
void LlmClientFacade::buildClients() {
  clients_.clear();

  for (auto &pc : configs_) {
    // 构建 OpenAICompatibleConfig
    OpenAICompatibleConfig oaiConfig;
    oaiConfig.baseUrl = pc.baseUrl;
    oaiConfig.model = pc.model;
    oaiConfig.apiKeyEnv = pc.apiKeyEnv;
    oaiConfig.apiKey = pc.apiKey;
    oaiConfig.timeoutSeconds = pc.timeoutSeconds;

    if (OpenAICompatibleClient::isConfigured(oaiConfig)) {
      clients_[pc.key] = std::make_shared<OpenAICompatibleClient>(oaiConfig);
      pc.available = true;
    } else {
      // 未配置 → 降级到 MockLlmClient
      clients_[pc.key] = std::make_shared<MockLlmClient>();
      pc.available = false;
    }
  }
}

// ============================================================
// Provider 查找
// ============================================================
const ProviderConfig *
LlmClientFacade::findProvider(const std::string &key) const {
  for (const auto &pc : configs_) {
    if (pc.key == key) {
      return &pc;
    }
  }
  return nullptr;
}

// ============================================================
// Provider 解析：请求指定 → 默认 → 第一个可用 → "mock"
// ============================================================
std::string
LlmClientFacade::resolveProvider(const std::string &requested) const {
  // 1. 显式指定且存在且可用 → 直接使用
  if (requested != "auto") {
    const auto *pc = findProvider(requested);
    if (pc && pc->available) {
      return requested;
    }
    // 指定了但不可用 → 继续尝试 default
  }

  // 2. 检查默认 provider
  if (!defaultProvider_.empty()) {
    const auto *pc = findProvider(defaultProvider_);
    if (pc && pc->available) {
      return defaultProvider_;
    }
  }

  // 3. 扫描第一个可用的 provider
  for (const auto &pc : configs_) {
    if (pc.available) {
      return pc.key;
    }
  }

  // 4. 都没有 → 使用 mock（第一个不可用的）
  if (!configs_.empty()) {
    return configs_[0].key;
  }

  return "mock";
}

// ============================================================
// 核心原子操作：chat
// ============================================================
LlmResponse LlmClientFacade::chat(const std::string &prompt,
                                  const std::string &provider,
                                  const std::string &model, int timeout) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!initialized_) {
    LlmResponse resp;
    resp.errorKind = LlmErrorKind::NotConfigured;
    resp.error = "LlmClientFacade not initialized";
    return resp;
  }

  const std::string resolved = resolveProvider(provider);
  auto it = clients_.find(resolved);
  if (it == clients_.end()) {
    // 退路：使用 MockLlmClient
    MockLlmClient mock;
    LlmRequest req;
    req.prompt = prompt;
    return mock.chat(req);
  }

  LlmRequest req;
  req.prompt = prompt;

  // model 覆盖
  if (!model.empty()) {
    req.model = model;
  } else {
    const auto *pc = findProvider(resolved);
    if (pc) {
      req.model = pc->model;
    }
  }

  // timeout 覆盖
  req.timeoutSeconds = timeout > 0 ? timeout : 0;

  return it->second->chat(req);
}

// ============================================================
// 流式 chat（第 1 点优化）
// ============================================================
void LlmClientFacade::chatStream(const std::string &prompt,
                                 OnTokenCallback onToken,
                                 const std::string &provider,
                                 const std::string &model, int timeout) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!initialized_) {
    onToken("LlmClientFacade not initialized");
    return;
  }

  const std::string resolved = resolveProvider(provider);
  auto it = clients_.find(resolved);
  if (it == clients_.end()) {
    MockLlmClient mock;
    LlmRequest req;
    req.prompt = prompt;
    auto resp = mock.chat(req);
    if (resp.success && !resp.content.empty()) {
      onToken(resp.content);
    }
    return;
  }

  LlmRequest req;
  req.prompt = prompt;
  if (!model.empty()) {
    req.model = model;
  } else {
    const auto *pc = findProvider(resolved);
    if (pc) {
      req.model = pc->model;
    }
  }
  req.timeoutSeconds = timeout > 0 ? timeout : 0;

  it->second->chatStream(req, onToken);
}

// ============================================================
// 健康检查
// ============================================================
bool LlmClientFacade::isAvailable() const {
  std::lock_guard<std::mutex> lock(mutex_);
  for (const auto &pc : configs_) {
    if (pc.available) {
      return true;
    }
  }
  return false;
}

bool LlmClientFacade::isProviderAvailable(const std::string &provider) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto *pc = findProvider(provider);
  return pc && pc->available;
}

std::string LlmClientFacade::getDefaultProvider() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return defaultProvider_;
}

// ============================================================
// Provider 管理
// ============================================================
std::vector<std::string> LlmClientFacade::listProviders() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<std::string> names;
  for (const auto &pc : configs_) {
    names.push_back(pc.key);
  }
  return names;
}

std::vector<ProviderConfig> LlmClientFacade::listProviderConfigs() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return configs_;
}

std::string LlmClientFacade::getCurrentProviderName() const {
  std::lock_guard<std::mutex> lock(mutex_);
  const std::string resolved = resolveProvider("auto");
  const auto *pc = findProvider(resolved);
  if (pc) {
    return pc->name;
  }
  return "Mock (" + resolved + ")";
}

// ============================================================
// 配置热加载
// ============================================================
bool LlmClientFacade::reloadConfig(const std::string &configPath) {
  std::lock_guard<std::mutex> lock(mutex_);
  const std::string path = configPath.empty() ? configPath_ : configPath;
  if (path.empty()) {
    return false;
  }
  try {
    // 保存旧 default
    const std::string oldDefault = defaultProvider_;

    loadProvidersFromConfig(path);
    loadLocalOverrides(path);
    buildClients();

    // 如果加载后 default 为空，恢复旧的
    if (defaultProvider_.empty()) {
      defaultProvider_ = oldDefault;
    }

    configPath_ = path;
    return true;
  } catch (const std::exception &) {
    return false;
  }
}

} // namespace codepilot
