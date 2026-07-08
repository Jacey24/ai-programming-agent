#include "ResponseParser.h"

namespace codepilot {

ParsedResponse ResponseParser::parse(const std::string& /*rawResponse*/) {
    // Sprint 1：忽略输入，返回固定假结果
    // Sprint 2：解析真实 LLM 返回的 XML / function_call

    ParsedResponse resp;
    resp.type = ResponseType::FinalAnswer;
    resp.content = "Sprint 1 假执行完成";
    return resp;
}

} // namespace codepilot