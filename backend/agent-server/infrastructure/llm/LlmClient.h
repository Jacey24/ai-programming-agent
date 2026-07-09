#pragma once

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

class LlmClient {
public:
  virtual ~LlmClient() = default;

  virtual LlmResponse chat(const LlmRequest &request) = 0;
};

class MockLlmClient final : public LlmClient {
public:
  LlmResponse chat(const LlmRequest &request) override;
};

} // namespace codepilot
