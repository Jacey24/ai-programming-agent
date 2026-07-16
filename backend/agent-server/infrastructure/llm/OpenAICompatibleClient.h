#pragma once

#include "infrastructure/llm/LlmClient.h"

#include <string>

namespace codepilot {

struct OpenAICompatibleConfig {
  std::string baseUrl;
  std::string model;
  std::string apiKeyEnv;
  std::string apiKey;
  std::string apiKeyFile;
  int timeoutSeconds{120};
};

class OpenAICompatibleClient final : public LlmClient {
public:
  explicit OpenAICompatibleClient(OpenAICompatibleConfig config);

  static OpenAICompatibleConfig loadConfig(const std::string &path);
  static bool isConfigured(const OpenAICompatibleConfig &config);
  static std::string resolveApiKey(const OpenAICompatibleConfig &config);

  LlmResponse chat(const LlmRequest &request) override;
  void chatStream(const LlmRequest &request, OnTokenCallback onToken) override;

  // Close sockets belonging to an in-flight request for this task. Safe to
  // call from the task cancellation thread while chat()/chatStream() blocks.
  static void cancelRequests(
      const std::shared_ptr<std::atomic<bool>> &cancelFlag);

private:
  OpenAICompatibleConfig config_;
};

} // namespace codepilot
