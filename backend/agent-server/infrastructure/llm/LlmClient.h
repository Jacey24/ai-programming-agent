#pragma once

#include <functional>
#include <string>

namespace codepilot {

struct LlmRequest {
  std::string prompt;
  std::string model;
  int timeoutSeconds{120};
};

struct LlmResponse {
  bool success{false};
  std::string content;
  std::string error;
  bool usedFallback{false};
};

using OnTokenCallback = std::function<void(const std::string &chunk)>;

class LlmClient {
public:
  virtual ~LlmClient() = default;

  virtual LlmResponse chat(const LlmRequest &request) = 0;

  // 流式 chat（第 1 点优化）
  // 默认实现：调用 chat() 后一次性回调全部内容。
  // 子类（如 OpenAICompatibleClient）可覆盖以实现逐 token 推送。
  virtual void chatStream(const LlmRequest &request, OnTokenCallback onToken) {
    auto resp = chat(request);
    if (resp.success && !resp.content.empty()) {
      onToken(resp.content);
    }
  }
};

class MockLlmClient final : public LlmClient {
public:
  LlmResponse chat(const LlmRequest &request) override;
};

} // namespace codepilot
