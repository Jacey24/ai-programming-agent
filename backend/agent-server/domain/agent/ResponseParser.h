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

// 单条解析命令
struct ParsedCommand {
    std::string toolName;    // "<cmd>READ src/main.cpp</cmd>" → "READ"
    std::string toolArgs;    // 命令参数部分
};

struct ParsedResponse {
    ResponseType type = ResponseType::Unknown;
    std::string toolName;
    std::string toolArgs;        // JSON 字符串
    std::string content;
    std::vector<PlanStep> planSteps;

    // Sprint 2 新增：支持一次 LLM 调用输出多个命令
    std::vector<ParsedCommand> commands;  // 解析出的所有 <cmd>
    bool isDone = false;                  // 是否以 DONE 结尾
    bool isFail = false;                  // 是否以 FAIL 结尾
    std::string failReason;               // FAIL 原因描述
};

class ResponseParser {
public:
    // 主解析入口：返回第一个工具调用（向后兼容）
    static ParsedResponse parse(const std::string& rawResponse);

    // 批量解析：返回所有命令，并判断 DONE/FAIL
    static ParsedResponse parseAll(const std::string& rawResponse);

private:
    // XML 标签提取
    static std::vector<std::string> extractTags(
        const std::string& content, const std::string& tag);

    // 提取标签内容（无属性）
    static std::string extractTagContent(
        const std::string& content, const std::string& tag);

    // 检查终止标记
    static bool hasDoneMarker(const std::string& content);
    static std::string extractFailReason(const std::string& content);
};

} // namespace codepilot