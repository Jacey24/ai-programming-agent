#pragma once

#include "TaskQueue.h"
#include <string>
#include <vector>

namespace codepilot {

enum class ResponseType {
    ToolCall,
    FinalAnswer,
    Plan,
    Unknown
};

struct ParsedResponse {
    ResponseType type = ResponseType::Unknown;
    std::string toolName;
    std::string toolArgs;   // JSON 字符串
    std::string content;
    std::vector<PlanStep> planSteps;
};

class ResponseParser {
public:
    // Sprint 1：返回固定假解析；Sprint 2：真实格式化解析
    static ParsedResponse parse(const std::string& rawResponse);
};

} // namespace codepilot