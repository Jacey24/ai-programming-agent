#include "infrastructure/llm/LlmClient.h"

namespace codepilot {

LlmResponse MockLlmClient::chat(const LlmRequest &request) {
  LlmResponse response;
  response.success = true;
  response.usedFallback = true;

  if (request.prompt.find("<plan>") != std::string::npos ||
      request.prompt.find("</plan>") != std::string::npos) {
    response.content =
        "<plan>\n"
        "  <task skill=\"executor\">分析任务需求</task>\n"
        "  <task skill=\"executor\">检查项目结构</task>\n"
        "  <task skill=\"executor\">执行核心操作</task>\n"
        "  <task skill=\"executor\">验证执行结果</task>\n"
        "</plan>";
    return response;
  }

  response.content = "DONE: mock fallback completed this step";
  return response;
}

} // namespace codepilot
