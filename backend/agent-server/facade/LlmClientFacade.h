#pragma once

#include "infrastructure/llm/LlmClient.h"
#include "infrastructure/llm/OpenAICompatibleClient.h"

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace codepilot {

// ============================================================
// 单个 Provider 的运行时配置
// ============================================================
struct ProviderConfig {
  std::string key;  // provider 键名（如 "doubao", "openai"）
  std::string name; // 显示名称（如 "豆包 (Doubao)"）
  std::string baseUrl;
  std::string model;
  std::string apiKeyEnv;
  std::string apiKey; // 来自 llm.local.json 覆盖
  int timeoutSeconds{120};
  bool available{false}; // 是否已成功配置
};

// ============================================================
// LlmClientFacade - LLM 调用统一入口（单例门面 Facade）
//
// 设计目标（参照 application/ToolSystem.h）：
//   一行 init() → 多 provider 自动注册 → chat() 原子操作
//
// 支持 6 个内置 Provider：
//   doubao, deepseek, openai, qwen, zhipu, moonshot
//
// 配置：
//   llm.json           → 定义所有 provider 和默认 provider
//   llm.local.json     → 覆盖各 provider 的 api_key（不提交 git）
//
// 线程安全（provider 表初始化后只读，无需锁）
// ============================================================
class LlmClientFacade {
public:
  static LlmClientFacade &getInstance();

  // ============================================================
  // 初始化（幂等，重复调用不生效）
  //   configPath: 主配置文件路径（默认 "config/llm.json"）
  //   自动加载 llm.json + llm.local.json
  //   为每个已配置的 provider 创建 OpenAICompatibleClient
  //   未配置的 provider 自动降级到 MockLlmClient
  // ============================================================
  void init(const std::string &configPath = "config/llm.json");
  bool isInitialized() const { return initialized_; }

  // ============================================================
  // 核心原子操作：发送消息到 LLM（阻塞）
  // ============================================================
  LlmResponse chat(const std::string &prompt,
                   const std::string &provider = "auto",
                   const std::string &model = "", int timeout = 0);

  // ============================================================
  // 流式 chat（第 1 点优化）
  //   默认行为等同于先 chat() 再一次性回调全部内容。
  //   若底层 LlmClient 支持流式，则逐 token 回调。
  // ============================================================
  void chatStream(const std::string &prompt, OnTokenCallback onToken,
                  const std::string &provider = "auto",
                  const std::string &model = "", int timeout = 0);

  // ============================================================
  // 健康检查
  // ============================================================
  bool isAvailable() const; // 是否有至少一个真实 provider
  bool isProviderAvailable(const std::string &provider) const;
  std::string getDefaultProvider() const; // 返回默认 provider 键名

  // ============================================================
  // Provider 管理
  // ============================================================
  std::vector<std::string> listProviders() const; // 所有已注册 provider 键名
  std::vector<ProviderConfig>
  listProviderConfigs() const;                // 含可用状态的完整配置
  std::string getCurrentProviderName() const; // 默认 provider 的显示名称

  // ============================================================
  // 配置热加载
  // ============================================================
  bool reloadConfig(const std::string &configPath = "");

private:
  LlmClientFacade() = default;
  ~LlmClientFacade() = default;
  LlmClientFacade(const LlmClientFacade &) = delete;
  LlmClientFacade &operator=(const LlmClientFacade &) = delete;

  void loadProvidersFromConfig(const std::string &configPath);
  void loadLocalOverrides(const std::string &configPath);
  void buildClients();
  const ProviderConfig *findProvider(const std::string &key) const;
  std::string resolveProvider(const std::string &requested) const;

  mutable std::mutex mutex_;
  std::string defaultProvider_;
  std::vector<ProviderConfig> configs_;
  std::unordered_map<std::string, std::shared_ptr<LlmClient>> clients_;
  std::string configPath_;
  bool initialized_ = false;
};

} // namespace codepilot