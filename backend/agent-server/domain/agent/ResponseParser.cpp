#include "ResponseParser.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace codepilot {

// ============================================================
// 内部辅助
// ============================================================

namespace {

// 去除首尾空白
std::string trim(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) {
        ++start;
    }
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
        --end;
    }
    return s.substr(start, end - start);
}

} // namespace

// ============================================================
// 公开接口
// ============================================================

ParsedResponse ResponseParser::parse(const std::string& rawResponse) {
    // 向后兼容：如果已有批量解析，优先用批量
    ParsedResponse result = parseAll(rawResponse);
    if (!result.commands.empty()) {
        result.type = ResponseType::ToolCall;
        result.toolName = result.commands[0].toolName;
        result.toolArgs = result.commands[0].toolArgs;
    }
    return result;
}

ParsedResponse ResponseParser::parseAll(const std::string& rawResponse) {
    ParsedResponse resp;

    // 1. 提取 <plan> 标签
    resp.planSteps.clear();
    std::string planContent = extractTagContent(rawResponse, "plan");
    if (!planContent.empty()) {
        // 解析 <plan> 内的 <task> 标签
        std::vector<std::string> tasks = extractTags(planContent, "task");
        for (const auto& task : tasks) {
            PlanStep step;
            step.role = "executor";

            // 提取 skill 属性
            const auto skillPos = task.find("skill=\"");
            if (skillPos != std::string::npos) {
                const auto valueStart = skillPos + 7;
                const auto valueEnd = task.find('"', valueStart);
                if (valueEnd != std::string::npos) {
                    step.role = task.substr(valueStart, valueEnd - valueStart);
                }
            }

            // 提取 fallback 属性
            if (task.find("fallback=\"true\"") != std::string::npos) {
                step.role = "fallback_" + step.role;
            }

            // 提取标签体作为 action 描述
            const auto gtPos = task.find('>');
            const auto ltPos = task.rfind("</task");
            if (gtPos != std::string::npos && ltPos != std::string::npos && ltPos > gtPos) {
                step.action = trim(task.substr(gtPos + 1, ltPos - gtPos - 1));
            }

            if (!step.action.empty()) {
                resp.planSteps.push_back(step);
            }
        }

        if (!resp.planSteps.empty()) {
            resp.type = ResponseType::Plan;
        }
    }

    // 2. 提取所有 <cmd> 标签
    std::vector<std::string> cmdTags = extractTags(rawResponse, "cmd");
    for (const auto& cmd : cmdTags) {
        std::string content = trim(cmd);
        // 格式: "TOOL_NAME arguments..." 或 "TOOL_NAME"
        const auto spacePos = content.find(' ');
        if (spacePos != std::string::npos) {
            ParsedCommand pc;
            pc.toolName = content.substr(0, spacePos);
            pc.toolArgs = trim(content.substr(spacePos + 1));
            resp.commands.push_back(pc);
        } else if (!content.empty()) {
            ParsedCommand pc;
            pc.toolName = content;
            pc.toolArgs = "";
            resp.commands.push_back(pc);
        }
    }

    // 3. 提取所有 <invoke> 标签
    std::vector<std::string> invokeTags = extractTags(rawResponse, "invoke");
    for (const auto& invoke : invokeTags) {
        ParsedCommand pc;
        pc.toolName = "mcp.call";

        // 提取 server 属性
        const auto serverPos = invoke.find("server=\"");
        if (serverPos != std::string::npos) {
            const auto valueStart = serverPos + 8;
            const auto valueEnd = invoke.find('"', valueStart);
            if (valueEnd != std::string::npos) {
                pc.toolName = "mcp." + invoke.substr(valueStart, valueEnd - valueStart);
            }
        }

        // 提取 tool 属性
        const auto toolPos = invoke.find("tool=\"");
        if (toolPos != std::string::npos) {
            const auto valueStart = toolPos + 6;
            const auto valueEnd = invoke.find('"', valueStart);
            if (valueEnd != std::string::npos) {
                pc.toolName = pc.toolName + "." + invoke.substr(valueStart, valueEnd - valueStart);
            }
        }

        // 提取 args 属性作为 JSON
        const auto argsPos = invoke.find("args='");
        if (argsPos != std::string::npos) {
            const auto valueStart = argsPos + 6;
            const auto valueEnd = invoke.find('\'', valueStart);
            if (valueEnd != std::string::npos) {
                pc.toolArgs = invoke.substr(valueStart, valueEnd - valueStart);
            }
        }

        resp.commands.push_back(pc);
    }

    // 4. 检查 DONE / FAIL 标记
    resp.isDone = hasDoneMarker(rawResponse);
    resp.isFail = !resp.isDone && !rawResponse.empty();
    resp.failReason = extractFailReason(rawResponse);

    if (resp.isDone) {
        resp.type = ResponseType::FinalAnswer;
        // 提取 DONE 后的总结文本
        const auto donePos = rawResponse.find("DONE:");
        if (donePos != std::string::npos) {
            const auto nlPos = rawResponse.find('\n', donePos + 5);
            if (nlPos != std::string::npos) {
                resp.content = trim(rawResponse.substr(donePos + 5, nlPos - donePos - 5));
            } else {
                resp.content = trim(rawResponse.substr(donePos + 5));
            }
        } else {
            // DONE 无冒号版本
            const auto doneSimple = rawResponse.find("DONE");
            if (doneSimple != std::string::npos) {
                resp.content = "任务完成";
            }
        }
    } else if (resp.isFail && !resp.commands.empty()) {
        // 有命令但没有 DONE → 继续执行，不算失败
        resp.isFail = false;
        resp.type = ResponseType::ToolCall;
    } else if (resp.isFail) {
        resp.type = ResponseType::Unknown;
    }

    // 如果有命令但类型未设置，默认 ToolCall
    if (!resp.commands.empty() && resp.type == ResponseType::Unknown) {
        resp.type = ResponseType::ToolCall;
    }

    // 如果既没有命令、没有 plan、没有 DONE，标记为 Unknown
    if (resp.commands.empty() && resp.planSteps.empty() && !resp.isDone && !resp.isFail) {
        resp.type = ResponseType::Unknown;
    }

    return resp;
}

// ============================================================
// 私有实现
// ============================================================

std::vector<std::string> ResponseParser::extractTags(
    const std::string& content, const std::string& tag) {
    std::vector<std::string> results;
    const std::string openTag = "<" + tag;
    const std::string closeTag = "</" + tag + ">";

    size_t pos = 0;
    while (pos < content.size()) {
        // 找开始标签
        const auto openPos = content.find(openTag, pos);
        if (openPos == std::string::npos) break;

        // 判断是否自闭合 <invoke .../>
        const auto selfClosePos = content.find("/>", openPos + openTag.size());
        const auto nextOpenPos = content.find('<', openPos + openTag.size());

        // 如果找到自闭合且它在下一个 '<' 之前 → 自闭合标签
        if (selfClosePos != std::string::npos &&
            (nextOpenPos == std::string::npos || selfClosePos < nextOpenPos)) {
            results.push_back(content.substr(openPos + 1, selfClosePos - openPos + 1));
            pos = selfClosePos + 2;
            continue;
        }

        // 找对应闭合标签
        const auto closePos = content.find(closeTag, openPos + openTag.size());
        if (closePos == std::string::npos) break;

        // 提取标签体（不含开始和闭合标签本身）
        const auto bodyStart = content.find('>', openPos);
        if (bodyStart == std::string::npos || bodyStart > closePos) break;

        const std::string body = content.substr(bodyStart + 1, closePos - bodyStart - 1);
        results.push_back(body);
        pos = closePos + closeTag.size();
    }

    return results;
}

std::string ResponseParser::extractTagContent(
    const std::string& content, const std::string& tag) {
    const std::string openTag = "<" + tag + ">";
    const std::string closeTag = "</" + tag + ">";

    const auto openPos = content.find(openTag);
    if (openPos == std::string::npos) return "";

    const auto closePos = content.find(closeTag, openPos + openTag.size());
    if (closePos == std::string::npos) return "";

    return content.substr(openPos + openTag.size(), closePos - openPos - openTag.size());
}

bool ResponseParser::hasDoneMarker(const std::string& content) {
    // 匹配 "DONE:" 或 "DONE"（行首或前面有空白）
    return content.find("DONE:") != std::string::npos ||
           content.find("DONE\n") != std::string::npos ||
           (content.size() >= 4 && content.compare(content.size() - 4, 4, "DONE") == 0);
}

std::string ResponseParser::extractFailReason(const std::string& content) {
    const auto failPos = content.find("FAIL:");
    if (failPos == std::string::npos) {
        const auto failSimple = content.find("FAIL\n");
        if (failSimple != std::string::npos) {
            return "任务执行失败";
        }
        // 检查是否以 FAIL 结尾
        if (content.size() >= 4 && content.compare(content.size() - 4, 4, "FAIL") == 0) {
            return "任务执行失败";
        }
        return "";
    }

    const auto nlPos = content.find('\n', failPos + 5);
    if (nlPos != std::string::npos) {
        return trim(content.substr(failPos + 5, nlPos - failPos - 5));
    }
    return trim(content.substr(failPos + 5));
}

} // namespace codepilot