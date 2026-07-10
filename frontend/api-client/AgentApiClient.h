#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace codepilot {

// Result returned by every API call.
struct ApiResult {
    bool success{false};
    nlohmann::json data;   // parsed "data" field from the response envelope
    std::string error;     // human-readable error message when success == false
    int httpStatus{0};     // raw HTTP status code (0 if transport failed)
};

// Options forwarded to the backend when creating a task.
struct TaskOptions {
    bool autoRunSafeCommands{true};
    bool requirePermissionForFileWrite{true};
    int maxSteps{10};
};

// Synchronous REST client for the CodePilot agent-server API.
//
// All methods block until the server responds (or the request times out).
// Internally, requests are made via a child `curl` process so that no
// extra library linkage is required.
class AgentApiClient {
public:
    explicit AgentApiClient(std::string baseUrl = "http://127.0.0.1:8080/api/v1");

    // --- Health -------------------------------------------------------
    ApiResult checkHealth();

    // --- Sessions -----------------------------------------------------
    ApiResult createSession(const std::string& title);

    // --- Workspaces ---------------------------------------------------
    ApiResult createWorkspace(const std::string& name, const std::string& path);

    // --- Tasks --------------------------------------------------------
    ApiResult createTask(const std::string& sessionId,
                         const std::string& workspaceId,
                         const std::string& input,
                         const TaskOptions& options = {});
    ApiResult getTask(const std::string& taskId);
    ApiResult cancelTask(const std::string& taskId,
                         const std::string& reason = "");
    ApiResult listTasks(int page = 1, int pageSize = 20);

    // --- Task artifacts -----------------------------------------------
    ApiResult getTaskLogs(const std::string& taskId);
    ApiResult getToolCalls(const std::string& taskId);
    ApiResult getFileChanges(const std::string& taskId);
    ApiResult getReplay(const std::string& taskId);

    // --- Permissions --------------------------------------------------
    ApiResult getPendingPermissions();
    ApiResult approvePermission(const std::string& permissionId);
    ApiResult rejectPermission(const std::string& permissionId,
                               const std::string& reason = "");

private:
    std::string baseUrl_;

    ApiResult get(const std::string& path);
    ApiResult post(const std::string& path, const nlohmann::json& body);

    // Runs `curl` via std::system() and captures the response body.
    ApiResult executeRequest(const std::string& method,
                             const std::string& url,
                             const std::string& body);

    static std::string urlEncode(const std::string& value);
};

} // namespace codepilot
