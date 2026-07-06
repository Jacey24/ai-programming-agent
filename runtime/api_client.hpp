// api_client.hpp — HTTP-based AI API client for LLM calls
#pragma once
#include <string>

namespace astral {

struct AgentConfig; // forward declaration

// Result of an API chat call
struct ChatResult {
  std::string content;  // parsed AI response text
  std::string raw_json; // full raw JSON response
};

// Handles HTTP communication with LLM API providers (e.g., DeepSeek)
class ApiClient {
public:
  ApiClient(const AgentConfig &cfg);

  // Send a chat completion request with system prompt + user input
  // Returns parsed content + raw JSON for debugging/token tracking
  ChatResult chat(const std::string &system_prompt,
                  const std::string &user_input, double temperature) const;

  // Extract token usage from raw JSON response
  static void extract_tokens(const std::string &raw_json, int &prompt_tokens,
                             int &completion_tokens, int &total_tokens);

private:
  const AgentConfig &cfg_;

  // Raw HTTP POST, returns full API response JSON
  std::string http_post(const std::string &url, const std::string &body) const;

  // Extract "content" field from API response JSON
  std::string parse_content(const std::string &resp) const;

  // Build the JSON request body
  std::string build_body(const std::string &system_prompt,
                         const std::string &user_input,
                         double temperature) const;
};

} // namespace astral