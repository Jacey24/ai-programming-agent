#include "Agent.h"
#include "ContextBuilder.h"
#include "LlmProvider.h"
#include "PromptAdapter.h"
#include "ResponseParser.h"
#include "Planner.h"
#include <chrono>
#include <ctime>

namespace codepilot {

Agent::Agent(RoleRegistry& registry, Planner& planner)
    : registry_(registry), planner_(planner) {}

AgentResult Agent::executeTask(
    const std::string& taskId,
    const std::string& sessionId,
    const std::string& workspaceId,
    const std::string& goal)
{
    std::string now = iso8601Now();
    auto steps = planner_.generatePlan(goal);
    queue_.loadFromPlan(steps);

    while (queue_.hasNext() && context_.size() < static_cast<size_t>(config_.maxSteps)) {
        PlanStep step = queue_.current();
        const RoleConfig* role = registry_.findByName(step.role);
        if (!role) {
            context_.push_back("[error] role not registered: " + step.role);
            queue_.markComplete();
            continue;
        }
        std::string roundCtx = ContextBuilder::buildRound(
            context_, *role, step.action, role->visibleTools);
        context_.push_back("[round] " + roundCtx);
        std::string prompt = PromptAdapter::buildPrompt(
            LlmProviderType::Generic, *role, goal, step.action,
            roundCtx, toolsToString(role->visibleTools));
        context_.push_back("[prompt] " + prompt);
        std::string fakeResponse = "[mock LLM] done: " + step.action;
        context_.push_back("[response] " + fakeResponse);
        ParsedResponse parsed = ResponseParser::parse(fakeResponse);
        if (parsed.type == ResponseType::FinalAnswer) {
            context_.push_back("[final] " + parsed.content);
            queue_.markComplete();
            break;
        }
        queue_.markComplete();
    }
    std::string doneTime = iso8601Now();
    AgentResult result;
    result.taskId = taskId;
    result.sessionId = sessionId;
    result.workspaceId = workspaceId;
    result.goal = goal;
    result.status = queue_.deriveStatusString();
    result.planJson = planToJson(steps);
    result.currentStep = steps.empty() ? "none" : steps.back().action;
    result.createdAt = now;
    result.updatedAt = doneTime;
    return result;
}

std::string Agent::toolsToString(const std::vector<std::string>& tools) {
    std::string s;
    for (size_t i = 0; i < tools.size(); ++i) {
        if (i > 0) s += ", ";
        s += tools[i];
    }
    return s;
}
std::string Agent::planToJson(const std::vector<PlanStep>& steps) {
    std::string json = "[";
    for (size_t i = 0; i < steps.size(); ++i) {
        if (i > 0) json += ",";
        json += "\"" + escapeJson(steps[i].action) + "\"";
    }
    json += "]";
    return json;
}
std::string Agent::escapeJson(const std::string& s) {
    std::string out;
    for (char ch : s) {
        switch (ch) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            default:   out += ch;
        }
    }
    return out;
}
std::string Agent::iso8601Now() {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    std::tm* tm = std::gmtime(&t);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", tm);
    return buf;
}
} // namespace codepilot