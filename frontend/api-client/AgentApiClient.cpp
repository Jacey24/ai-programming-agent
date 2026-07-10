#include "api-client/AgentApiClient.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

using json = nlohmann::json;

namespace codepilot {

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

namespace {

std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return "";
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool writeFile(const std::string& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }
    out << content;
    return true;
}

// Cross-platform shell quoting for curl arguments.
std::string shellQuote(const std::string& value) {
#ifdef _WIN32
    std::string quoted = "\"";
    for (char ch : value) {
        if (ch == '"') {
            quoted += "\\\"";
        } else {
            quoted += ch;
        }
    }
    quoted += "\"";
    return quoted;
#else
    std::string quoted = "'";
    for (char ch : value) {
        if (ch == '\'') {
            quoted += "'\\''";
        } else {
            quoted += ch;
        }
    }
    quoted += "'";
    return quoted;
#endif
}

// Generate a path for a temporary file under the OS temp directory.
std::string tempPath(const std::string& label) {
    const char* tmpDir = std::getenv("TMPDIR");
    if (!tmpDir || !*tmpDir) {
        tmpDir = std::getenv("TEMP");
    }
    if (!tmpDir || !*tmpDir) {
        tmpDir = ".";
    }

    std::string dir = tmpDir;
    const char sep =
#ifdef _WIN32
        '\\';
#else
        '/';
#endif
    if (!dir.empty() && dir.back() != '/' && dir.back() != '\\') {
        dir.push_back(sep);
    }
    return dir + "codepilot_api_" + label + "_" +
           std::to_string(std::rand()) + ".json";
}

} // namespace

// ---------------------------------------------------------------------------
// AgentApiClient
// ---------------------------------------------------------------------------

AgentApiClient::AgentApiClient(std::string baseUrl)
    : baseUrl_(std::move(baseUrl)) {
    while (!baseUrl_.empty() && baseUrl_.back() == '/') {
        baseUrl_.pop_back();
    }
}

// --- Health ----------------------------------------------------------------

ApiResult AgentApiClient::checkHealth() {
    return get("/health");
}

// --- Sessions --------------------------------------------------------------

ApiResult AgentApiClient::createSession(const std::string& title) {
    json body;
    body["title"] = title;
    return post("/sessions", body);
}

// --- Workspaces ------------------------------------------------------------

ApiResult AgentApiClient::createWorkspace(const std::string& name,
                                           const std::string& path) {
    json body;
    body["name"] = name;
    body["path"] = path;
    return post("/workspaces", body);
}

// --- Tasks -----------------------------------------------------------------

ApiResult AgentApiClient::createTask(const std::string& sessionId,
                                      const std::string& workspaceId,
                                      const std::string& input,
                                      const TaskOptions& options) {
    json body;
    body["session_id"] = sessionId;
    body["workspace_id"] = workspaceId;
    body["input"] = input;
    body["options"]["auto_run_safe_commands"] = options.autoRunSafeCommands;
    body["options"]["require_permission_for_file_write"] =
        options.requirePermissionForFileWrite;
    body["options"]["max_steps"] = options.maxSteps;
    return post("/tasks", body);
}

ApiResult AgentApiClient::getTask(const std::string& taskId) {
    return get("/tasks/" + urlEncode(taskId));
}

ApiResult AgentApiClient::cancelTask(const std::string& taskId,
                                      const std::string& reason) {
    json body;
    body["reason"] = reason.empty() ? "用户取消" : reason;
    return post("/tasks/" + urlEncode(taskId) + "/cancel", body);
}

ApiResult AgentApiClient::listTasks(int page, int pageSize) {
    std::ostringstream path;
    path << "/tasks?page=" << page << "&page_size=" << pageSize;
    return get(path.str());
}

// --- Task artifacts --------------------------------------------------------

ApiResult AgentApiClient::getTaskLogs(const std::string& taskId) {
    return get("/tasks/" + urlEncode(taskId) + "/logs");
}

ApiResult AgentApiClient::getToolCalls(const std::string& taskId) {
    return get("/tasks/" + urlEncode(taskId) + "/tool-calls");
}

ApiResult AgentApiClient::getFileChanges(const std::string& taskId) {
    return get("/tasks/" + urlEncode(taskId) + "/file-changes");
}

ApiResult AgentApiClient::getReplay(const std::string& taskId) {
    return get("/tasks/" + urlEncode(taskId) + "/replay");
}

// --- Permissions -----------------------------------------------------------

ApiResult AgentApiClient::getPendingPermissions() {
    return get("/permissions/pending");
}

ApiResult AgentApiClient::approvePermission(const std::string& permissionId) {
    json body;
    body["remember"] = false;
    return post("/permissions/" + urlEncode(permissionId) + "/approve", body);
}

ApiResult AgentApiClient::rejectPermission(const std::string& permissionId,
                                            const std::string& reason) {
    json body;
    body["reason"] = reason.empty() ? "用户拒绝" : reason;
    return post("/permissions/" + urlEncode(permissionId) + "/reject", body);
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

ApiResult AgentApiClient::get(const std::string& path) {
    return executeRequest("GET", baseUrl_ + path, "");
}

ApiResult AgentApiClient::post(const std::string& path, const json& body) {
    return executeRequest("POST", baseUrl_ + path, body.dump());
}

ApiResult AgentApiClient::executeRequest(const std::string& method,
                                          const std::string& url,
                                          const std::string& body) {
    ApiResult result;

    const std::string responsePath = tempPath("resp");
    std::string requestPath;

    std::ostringstream cmd;
    cmd << "curl -sS --max-time 30";
    cmd << " -X " << method;
    cmd << " -H " << shellQuote("Content-Type: application/json");
    cmd << " -H " << shellQuote("Accept: application/json");

    if (!body.empty()) {
        requestPath = tempPath("req");
        if (!writeFile(requestPath, body)) {
            result.error = "failed to write temporary request body";
            return result;
        }
        cmd << " --data-binary @" << shellQuote(requestPath);
    }

    cmd << " -o " << shellQuote(responsePath);
    cmd << " " << shellQuote(url);

    const int exitCode = std::system(cmd.str().c_str());

    if (!requestPath.empty()) {
        std::remove(requestPath.c_str());
    }

    if (exitCode != 0) {
        std::remove(responsePath.c_str());
        result.error = "curl failed (exit code " + std::to_string(exitCode) + ")";
        return result;
    }

    const std::string responseText = readFile(responsePath);
    std::remove(responsePath.c_str());

    if (responseText.empty()) {
        // Treat an empty body as a successful response with no data (e.g. 204).
        result.success = true;
        result.data = json::object();
        return result;
    }

    try {
        const json parsed = json::parse(responseText);
        result.success = parsed.value("success", false);
        result.data = parsed.value("data", json::object());

        if (!result.success) {
            const json& errorField = parsed["error"];
            if (errorField.is_object()) {
                result.error = errorField.value("message", "API returned failure");
            } else if (errorField.is_string()) {
                result.error = errorField.get<std::string>();
            } else {
                result.error = parsed.value("message", "API returned failure");
            }
        }
    } catch (const std::exception& e) {
        result.error =
            std::string("failed to parse API response: ") + e.what();
    }

    return result;
}

std::string AgentApiClient::urlEncode(const std::string& value) {
    std::ostringstream encoded;
    encoded << std::hex << std::uppercase;
    for (const unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded << static_cast<char>(c);
        } else {
            encoded << '%' << static_cast<int>(c);
        }
    }
    return encoded.str();
}

} // namespace codepilot
