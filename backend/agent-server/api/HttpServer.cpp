#include "api/HttpServer.h"
#include "infrastructure/storage/SqliteConnection.h"

#include "api/controllers/ConfigController.h"
#include "api/controllers/DebugController.h"
#include "api/controllers/ExpertController.h"
#include "api/controllers/FileChangeController.h"
#include "api/controllers/GlobalController.h"
#include "api/controllers/LogController.h"
#include "api/controllers/PermissionController.h"
#include "api/controllers/ReplayController.h"
#include "api/controllers/SessionController.h"
#include "api/controllers/TaskController.h"
#include "api/controllers/ToolController.h"
#include "api/controllers/WorkspaceController.h"
#include "api/controllers/WorkspaceFileController.h"

#include "application/ToolSystem.h"
#include "event/EventBus.h"
#include "facade/DataAccessFacade.h"
#include "facade/SSEGateway.h"

#include "common/logging/Logger.h"

#include "httplib.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>
#include <utility>

#include <nlohmann/json.hpp>
#include <sqlite3.h>

namespace codepilot {

namespace {

std::string json_escape(const std::string &value) {
  std::ostringstream escaped;
  for (const char ch : value) {
    switch (ch) {
    case '"':
      escaped << "\\\"";
      break;
    case '\\':
      escaped << "\\\\";
      break;
    case '\n':
      escaped << "\\n";
      break;
    case '\r':
      escaped << "\\r";
      break;
    case '\t':
      escaped << "\\t";
      break;
    default:
      escaped << ch;
    }
  }
  return escaped.str();
}

std::string not_found_json() {
  return R"({"success":false,"error":{"code":"NOT_FOUND","message":"Endpoint not found"}})";
}

std::string extract_json_body(const std::string &httpResp) {
  auto pos = httpResp.find("\r\n\r\n");
  if (pos != std::string::npos) {
    return httpResp.substr(pos + 4);
  }
  pos = httpResp.find("\n\n");
  if (pos != std::string::npos) {
    return httpResp.substr(pos + 2);
  }
  return httpResp;
}

bool parse_http_status(const std::string &httpResp, int &status) {
  const auto lineEnd = httpResp.find('\n');
  if (lineEnd == std::string::npos) {
    return false;
  }

  std::string statusLine = httpResp.substr(0, lineEnd);
  if (!statusLine.empty() && statusLine.back() == '\r') {
    statusLine.pop_back();
  }
  if (statusLine.rfind("HTTP/", 0) != 0) {
    return false;
  }

  const auto codeStart = statusLine.find(' ');
  if (codeStart == std::string::npos) {
    return false;
  }
  const auto codeEnd = statusLine.find(' ', codeStart + 1);
  const std::string code = statusLine.substr(
      codeStart + 1, codeEnd == std::string::npos ? std::string::npos
                                                  : codeEnd - codeStart - 1);
  if (code.size() != 3 || code[0] < '0' || code[0] > '9' || code[1] < '0' ||
      code[1] > '9' || code[2] < '0' || code[2] > '9') {
    return false;
  }

  const int parsed =
      (code[0] - '0') * 100 + (code[1] - '0') * 10 + (code[2] - '0');
  if (parsed < 100 || parsed > 599) {
    return false;
  }
  status = parsed;
  return true;
}

std::string build_query_string(const httplib::Params &params) {
  if (params.empty())
    return "";
  std::ostringstream qs;
  bool first = true;
  for (const auto &p : params) {
    if (first)
      first = false;
    else
      qs << "&";
    qs << p.first << "=" << p.second;
  }
  return qs.str();
}

} // namespace

struct HttpServer::Impl {
  httplib::Server svr;
};

HttpServer::HttpServer(HttpServerConfig config)
    : config_(std::move(config)), impl_(std::make_unique<Impl>()) {}

HttpServer::~HttpServer() = default;

int HttpServer::run(const std::atomic_bool &running) {
  auto &svr = impl_->svr;
  const auto &cfg = config_;

  auto setCors = [](const httplib::Request &, httplib::Response &res) {
    res.set_header("Access-Control-Allow-Origin", "*");
    res.set_header("Access-Control-Allow-Methods",
                   "GET, POST, PUT, PATCH, DELETE, OPTIONS");
    res.set_header("Access-Control-Allow-Headers", "Content-Type");
  };

  svr.Options(R"(/api/v1/.*)",
              [setCors](const httplib::Request &req, httplib::Response &res) {
                setCors(req, res);
                res.status = 204;
              });

  // Health
  svr.Get("/api/v1/health", [setCors, &cfg](const httplib::Request &req,
                                            httplib::Response &res) {
    setCors(req, res);
    std::ostringstream body;
    body
        << R"({"success":true,"data":{"status":"ok","service":"codepilot-agent-server","version":"0.1.0","database":{"type":"sqlite","connected":)"
        << (cfg.databaseConnected ? "true" : "false") << R"(,"path":")"
        << json_escape(cfg.databasePath) << R"(")";
    if (!cfg.databaseError.empty()) {
      body << R"(,"error":")" << json_escape(cfg.databaseError) << R"(")";
    }
    body << "}}}";
    res.set_content(body.str(), "application/json; charset=utf-8");
  });

  svr.Get("/health",
          [setCors](const httplib::Request &req, httplib::Response &res) {
            setCors(req, res);
            res.set_content("OK", "text/plain");
          });

  // SSE
  svr.Get(R"(/api/v1/tasks/([a-zA-Z0-9\-_]+)/events)", [setCors](
                                                           const httplib::
                                                               Request &req,
                                                           httplib::Response
                                                               &res) {
    setCors(req, res);
    std::string taskId = req.matches[1].str();
    if (taskId.empty()) {
      res.status = 400;
      res.set_content(
          R"({"success":false,"error":{"code":"BAD_REQUEST","message":"Missing task_id"}})",
          "application/json");
      return;
    }
    struct SseContext {
      std::mutex mtx;
      std::condition_variable cv;
      std::deque<std::string> frames;
      bool streamDone{false};
      std::shared_ptr<SSEGateway::ConnectionSignal> connectionSignal{
          std::make_shared<SSEGateway::ConnectionSignal>()};
    };
    auto ctx = std::make_shared<SseContext>();
    res.set_chunked_content_provider(
        "text/event-stream",
        [ctx](size_t, httplib::DataSink &sink) -> bool {
          while (true) {
            std::string frame;
            {
              std::unique_lock<std::mutex> lock(ctx->mtx);
              if (!ctx->frames.empty()) {
                frame = std::move(ctx->frames.front());
                ctx->frames.pop_front();
              } else if (ctx->streamDone)
                break;
              else {
                ctx->cv.wait(lock, [&] {
                  return !ctx->frames.empty() || ctx->streamDone;
                });
                if (!ctx->frames.empty()) {
                  frame = std::move(ctx->frames.front());
                  ctx->frames.pop_front();
                } else if (ctx->streamDone)
                  break;
              }
            }
            if (!frame.empty()) {
              const bool writable =
                  !sink.is_writable || sink.is_writable();
              if (!writable || !sink.write(frame.data(), frame.size())) {
                {
                  std::lock_guard<std::mutex> lock(ctx->mtx);
                  ctx->streamDone = true;
                  ctx->frames.clear();
                }
                ctx->connectionSignal->close();
                ctx->cv.notify_all();
                return false;
              }
            }
          }
          sink.done();
          return true;
        },
        [ctx](bool) {
          {
            std::lock_guard<std::mutex> lock(ctx->mtx);
            ctx->streamDone = true;
            ctx->frames.clear();
          }
          ctx->connectionSignal->close();
          ctx->cv.notify_all();
        });
    auto sendFn = [ctx](const std::string &frame) -> bool {
      std::lock_guard<std::mutex> lock(ctx->mtx);
      if (ctx->streamDone) {
        return false;
      }
      ctx->frames.push_back(frame);
      ctx->cv.notify_one();
      return true;
    };
    std::thread([sendFn, taskId, ctx]() {
      SSEGateway::getInstance().streamTaskEvents(sendFn, taskId,
                                                 ctx->connectionSignal);

      {
        std::lock_guard<std::mutex> lock(ctx->mtx);
        ctx->streamDone = true;
      }
      ctx->cv.notify_all();
    }).detach();
  });

  const std::string &dbPath = cfg.databasePath;

  auto respondJson = [](httplib::Response &res, const std::string &httpResp) {
    int status = 0;
    if (parse_http_status(httpResp, status)) {
      res.status = status;
    } else {
      LOG_WARN("Controller returned a malformed HTTP status line; using "
               "httplib default status {}",
               res.status);
    }
    res.set_content(extract_json_body(httpResp),
                    "application/json; charset=utf-8");
  };

#define ROUTE(method, path, ctrlType, ctrlMethod, argExpr)                     \
  svr.method(path, [setCors, &dbPath, &respondJson](                           \
                       const httplib::Request &req, httplib::Response &res) {  \
    setCors(req, res);                                                         \
    ctrlType ctrl(dbPath);                                                     \
    respondJson(res, ctrl.ctrlMethod((argExpr)));                              \
  });

#define ROUTE0(method, path, ctrlType, ctrlMethod)                             \
  svr.method(path, [setCors, &dbPath, &respondJson](                           \
                       const httplib::Request &req, httplib::Response &res) {  \
    setCors(req, res);                                                         \
    ctrlType ctrl(dbPath);                                                     \
    respondJson(res, ctrl.ctrlMethod());                                       \
  });

#define ROUTE_NODB(method, path, ctrlType, ctrlMethod, argExpr)                \
  svr.method(path, [setCors, &respondJson](const httplib::Request &req,        \
                                           httplib::Response &res) {           \
    setCors(req, res);                                                         \
    ctrlType ctrl;                                                             \
    respondJson(res, ctrl.ctrlMethod((argExpr)));                              \
  });

#define ROUTE_NODB0(method, path, ctrlType, ctrlMethod)                        \
  svr.method(path, [setCors, &respondJson](const httplib::Request &req,        \
                                           httplib::Response &res) {           \
    setCors(req, res);                                                         \
    ctrlType ctrl;                                                             \
    respondJson(res, ctrl.ctrlMethod());                                       \
  });

  // ========== Global ==========
  ROUTE(Post, "/api/v1/globals", GlobalController, createGlobal,
        "POST /api/v1/globals \r\n\r\n" + req.body);
  ROUTE(Get, "/api/v1/globals", GlobalController, listGlobals,
        "GET /api/v1/globals?" + build_query_string(req.params));
  ROUTE(Get, R"(/api/v1/globals/([a-zA-Z0-9\-_]+))", GlobalController,
        getGlobal, "GET /api/v1/globals/" + req.matches[1].str());
  ROUTE(Get, R"(/api/v1/globals/([a-zA-Z0-9\-_]+)/context)", GlobalController,
        getGlobalContext,
        "GET /api/v1/globals/" + req.matches[1].str() + "/context");
  ROUTE(Delete, R"(/api/v1/globals/([a-zA-Z0-9\-_]+))", GlobalController,
        deleteGlobal, "DELETE /api/v1/globals/" + req.matches[1].str());

  // Session
  ROUTE(Post, "/api/v1/sessions", SessionController, createSession,
        "POST /api/v1/sessions \r\n\r\n" + req.body);
  ROUTE(Get, "/api/v1/sessions", SessionController, listSessions,
        "GET /api/v1/sessions?" + build_query_string(req.params));
  ROUTE(Get, R"(/api/v1/sessions/([a-zA-Z0-9\-_]+))", SessionController,
        getSession, "GET /api/v1/sessions/" + req.matches[1].str());
  ROUTE(Put, R"(/api/v1/sessions/([a-zA-Z0-9\-_]+))", SessionController,
        updateSession,
        "PUT /api/v1/sessions/" + req.matches[1].str() + " \r\n\r\n" +
            req.body);
  ROUTE(Delete, R"(/api/v1/sessions/([a-zA-Z0-9\-_]+))", SessionController,
        deleteSession, "DELETE /api/v1/sessions/" + req.matches[1].str());

  // Workspace
  ROUTE(Post, "/api/v1/workspaces/select-directory", WorkspaceController,
        selectLocalDirectory,
        "POST /api/v1/workspaces/select-directory \r\n\r\n" + req.body);
  ROUTE(Post, "/api/v1/workspaces", WorkspaceController, createWorkspace,
        "POST /api/v1/workspaces \r\n\r\n" + req.body);
  ROUTE(Get, "/api/v1/workspaces", WorkspaceController, listWorkspaces,
        "GET /api/v1/workspaces?" + build_query_string(req.params));
  ROUTE(Get, R"(/api/v1/workspaces/([a-zA-Z0-9\-_]+))", WorkspaceController,
        getWorkspace, "GET /api/v1/workspaces/" + req.matches[1].str());
  ROUTE(Get, R"(/api/v1/workspaces/([a-zA-Z0-9\-_]+)/files/tree)",
        WorkspaceFileController, getTree, "GET " + req.target);
  ROUTE(Get, R"(/api/v1/workspaces/([a-zA-Z0-9\-_]+)/files/content)",
        WorkspaceFileController, getFileContent, "GET " + req.target);
  ROUTE(Put, R"(/api/v1/workspaces/([a-zA-Z0-9\-_]+))", WorkspaceController,
        updateWorkspace,
        "PUT /api/v1/workspaces/" + req.matches[1].str() + " \r\n\r\n" +
            req.body);
  ROUTE(Delete, R"(/api/v1/workspaces/([a-zA-Z0-9\-_]+))", WorkspaceController,
        deleteWorkspace, "DELETE /api/v1/workspaces/" + req.matches[1].str());
  ROUTE(Get, R"(/api/v1/workspaces/([a-zA-Z0-9\-_]+)/sessions)",
        WorkspaceController, listSessions, "GET " + req.target);

  // Task
  ROUTE(Post, "/api/v1/tasks", TaskController, createTask,
        "POST /api/v1/tasks \r\n\r\n" + req.body);
  ROUTE(Post, "/api/v1/tasks/continue", TaskController, continueTask,
        "POST /api/v1/tasks/continue \r\n\r\n" + req.body);
  ROUTE(Get, "/api/v1/tasks", TaskController, listTasks,
        "GET /api/v1/tasks?" + build_query_string(req.params));
  ROUTE0(Get, "/api/v1/tasks/active", TaskController, listActiveTasks);
  ROUTE(Get, R"(/api/v1/tasks/([a-zA-Z0-9\-_]+))", TaskController, getTask,
        "GET /api/v1/tasks/" + req.matches[1].str());
  ROUTE(Get, R"(/api/v1/tasks/([a-zA-Z0-9\-_]+)/logs)", LogController, listLogs,
        "GET /api/v1/tasks/" + req.matches[1].str() + "/logs");
  ROUTE(Get, R"(/api/v1/tasks/([a-zA-Z0-9\-_]+)/file-changes)",
        FileChangeController, listFileChanges,
        "GET /api/v1/tasks/" + req.matches[1].str() + "/file-changes");
  ROUTE(Get, R"(/api/v1/tasks/([a-zA-Z0-9\-_]+)/replay)", ReplayController,
        getReplay, "GET /api/v1/tasks/" + req.matches[1].str() + "/replay");
  ROUTE(Get, R"(/api/v1/tasks/([a-zA-Z0-9\-_]+)/tool-calls)", TaskController,
        listToolCalls,
        "GET /api/v1/tasks/" + req.matches[1].str() + "/tool-calls");
  ROUTE(Get, R"(/api/v1/tasks/([a-zA-Z0-9\-_]+)/events/history)",
        TaskController, listEventHistory,
        "GET /api/v1/tasks/" + req.matches[1].str() + "/events/history");
  ROUTE(Delete, R"(/api/v1/tasks/([a-zA-Z0-9\-_]+))", TaskController,
        deleteTask, "DELETE /api/v1/tasks/" + req.matches[1].str());
  ROUTE(Post, R"(/api/v1/tasks/([a-zA-Z0-9\-_]+)/cancel)", TaskController,
        cancelTask, "POST /api/v1/tasks/" + req.matches[1].str() + "/cancel");

  // File Changes
  ROUTE(Get, R"(/api/v1/file-changes/([a-zA-Z0-9\-_]+))", FileChangeController,
        getFileChange, "GET /api/v1/file-changes/" + req.matches[1].str());

  // Tools
  ROUTE_NODB0(Get, "/api/v1/tools", ToolController, listTools);
  ROUTE_NODB(Get, R"(/api/v1/tools/([a-zA-Z0-9\-_]+))", ToolController,
             getToolDetail, "GET /api/v1/tools/" + req.matches[1].str());

  // Permissions
  ROUTE(Get, "/api/v1/permissions", PermissionController, listPermissions,
        "GET /api/v1/permissions?" + build_query_string(req.params));
  ROUTE(Get, R"(/api/v1/permissions/([a-zA-Z0-9\-_]+))", PermissionController,
        getPermission, "GET /api/v1/permissions/" + req.matches[1].str());
  ROUTE(Post, "/api/v1/permissions/approve-first", PermissionController,
        approveFirstPending, "POST /api/v1/permissions/approve-first");
  ROUTE(Post, R"(/api/v1/permissions/([a-zA-Z0-9\-_]+))", PermissionController,
        handleAction, "POST /api/v1/permissions/" + req.matches[1].str());
  ROUTE(Post, R"(/api/v1/permissions/([a-zA-Z0-9\-_]+)/approve)",
        PermissionController, handleAction,
        "POST /api/v1/permissions/" + req.matches[1].str() + "/approve");
  ROUTE(Post, R"(/api/v1/permissions/([a-zA-Z0-9\-_]+)/reject)",
        PermissionController, handleAction,
        "POST /api/v1/permissions/" + req.matches[1].str() + "/reject");

  // Expert
  ROUTE_NODB(Get, "/api/v1/experts/graph", ExpertController, getGraph,
             "GET /api/v1/experts/graph");
  ROUTE_NODB0(Get, "/api/v1/experts/graph/positions", ExpertController,
              getPositions);
  ROUTE_NODB(Put, "/api/v1/experts/graph/positions", ExpertController,
             savePositions,
             "PUT /api/v1/experts/graph/positions \r\n\r\n" + req.body);
  ROUTE_NODB0(Get, "/api/v1/experts/export", ExpertController, exportConfig);
  ROUTE_NODB(Post, "/api/v1/experts/import", ExpertController, importConfig,
             "POST /api/v1/experts/import \r\n\r\n" + req.body);
  ROUTE_NODB(Post, "/api/v1/experts/validate", ExpertController, validateConfig,
             "POST /api/v1/experts/validate \r\n\r\n" + req.body);
  ROUTE_NODB0(Get, "/api/v1/experts/llm/defaults", ExpertController,
              getGlobalLlmDefaults);
  ROUTE_NODB(Put, "/api/v1/experts/llm/defaults", ExpertController,
             setGlobalLlmDefaults,
             "PUT /api/v1/experts/llm/defaults \r\n\r\n" + req.body);
  ROUTE_NODB(Post, "/api/v1/experts", ExpertController, createExpert,
             "POST /api/v1/experts \r\n\r\n" + req.body);
  ROUTE_NODB0(Get, "/api/v1/experts", ExpertController, listExperts);
  ROUTE_NODB(Get, R"(/api/v1/experts/([a-zA-Z0-9\-_]+))", ExpertController,
             getExpert, "GET /api/v1/experts/" + req.matches[1].str());
  ROUTE_NODB(Delete, R"(/api/v1/experts/([a-zA-Z0-9\-_]+))", ExpertController,
             deleteExpert, "DELETE /api/v1/experts/" + req.matches[1].str());
  ROUTE_NODB(Put, R"(/api/v1/experts/([a-zA-Z0-9\-_]+))", ExpertController,
             updateExpert,
             "PUT /api/v1/experts/" + req.matches[1].str() + " \r\n\r\n" +
                 req.body);
  ROUTE_NODB(Patch, R"(/api/v1/experts/([a-zA-Z0-9\-_]+))", ExpertController,
             patchExpert,
             "PATCH /api/v1/experts/" + req.matches[1].str() + " \r\n\r\n" +
                 req.body);
  ROUTE_NODB(Put, R"(/api/v1/experts/([a-zA-Z0-9\-_]+)/llm)", ExpertController,
             setLlm,
             "PUT /api/v1/experts/" + req.matches[1].str() + "/llm \r\n\r\n" +
                 req.body);
  ROUTE_NODB(Get, R"(/api/v1/experts/([a-zA-Z0-9\-_]+)/prompt/preview)",
             ExpertController, previewPrompt,
             "GET /api/v1/experts/" + req.matches[1].str() + "/prompt/preview");
  ROUTE_NODB(Post, R"(/api/v1/experts/([a-zA-Z0-9\-_]+)/tools)",
             ExpertController, addTool,
             "POST /api/v1/experts/" + req.matches[1].str() +
                 "/tools \r\n\r\n" + req.body);
  ROUTE_NODB(Delete, R"(/api/v1/experts/([a-zA-Z0-9\-_]+)/tools)",
             ExpertController, removeTool,
             "DELETE /api/v1/experts/" + req.matches[1].str() +
                 "/tools \r\n\r\n" + req.body);
  ROUTE_NODB(Put, R"(/api/v1/experts/([a-zA-Z0-9\-_]+)/tools)",
             ExpertController, setTools,
             "PUT /api/v1/experts/" + req.matches[1].str() + "/tools \r\n\r\n" +
                 req.body);
  ROUTE_NODB(Post, R"(/api/v1/experts/([a-zA-Z0-9\-_]+)/routes)",
             ExpertController, addRoute,
             "POST /api/v1/experts/" + req.matches[1].str() +
                 "/routes \r\n\r\n" + req.body);
  ROUTE_NODB(Delete, R"(/api/v1/experts/([a-zA-Z0-9\-_]+)/routes)",
             ExpertController, removeRoute,
             "DELETE /api/v1/experts/" + req.matches[1].str() +
                 "/routes \r\n\r\n" + req.body);
  ROUTE_NODB(Put, R"(/api/v1/experts/([a-zA-Z0-9\-_]+)/routes)",
             ExpertController, setRoutes,
             "PUT /api/v1/experts/" + req.matches[1].str() +
                 "/routes \r\n\r\n" + req.body);

  // Config
  ROUTE_NODB0(Get, "/api/v1/config", ConfigController, getConfig);
  ROUTE_NODB0(Get, "/api/v1/config/llm", ConfigController, getLlmConfig);
  ROUTE_NODB(Put, "/api/v1/config/llm", ConfigController, setLlmConfig,
             "PUT /api/v1/config/llm \r\n\r\n" + req.body);
  ROUTE_NODB(Post, "/api/v1/config/llm/test", ConfigController,
             testLlmConnection,
             "POST /api/v1/config/llm/test \r\n\r\n" + req.body);
  ROUTE_NODB0(Get, "/api/v1/config/llm/providers", ConfigController,
              listLlmProviders);
  ROUTE_NODB(Post, "/api/v1/config/llm/providers", ConfigController,
             addLlmProvider,
             "POST /api/v1/config/llm/providers \r\n\r\n" + req.body);
  ROUTE_NODB(Put, R"(/api/v1/config/llm/providers/([a-zA-Z0-9\-_]+))",
             ConfigController, updateLlmProvider,
             "PUT /api/v1/config/llm/providers/" + req.matches[1].str() +
                 " \r\n\r\n" + req.body);
  ROUTE_NODB(Delete, R"(/api/v1/config/llm/providers/([a-zA-Z0-9\-_]+))",
             ConfigController, deleteLlmProvider,
             "DELETE /api/v1/config/llm/providers/" + req.matches[1].str());
  ROUTE_NODB0(Get, "/api/v1/config/agent", ConfigController, getAgentConfig);
  ROUTE_NODB(Put, "/api/v1/config/agent", ConfigController, setAgentConfig,
             "PUT /api/v1/config/agent \r\n\r\n" + req.body);
  ROUTE_NODB0(Get, "/api/v1/config/workspace", ConfigController,
              getWorkspaceConfig);
  ROUTE_NODB(Put, "/api/v1/config/workspace", ConfigController,
             setWorkspaceConfig,
             "PUT /api/v1/config/workspace \r\n\r\n" + req.body);
  ROUTE_NODB0(Get, "/api/v1/config/logging", ConfigController,
              getLoggingConfig);
  ROUTE_NODB(Put, "/api/v1/config/logging", ConfigController, setLoggingConfig,
             "PUT /api/v1/config/logging \r\n\r\n" + req.body);
  ROUTE_NODB0(Get, "/api/v1/config/tools", ConfigController, getToolsConfig);
  ROUTE_NODB(Put, "/api/v1/config/tools", ConfigController, setToolsConfig,
             "PUT /api/v1/config/tools \r\n\r\n" + req.body);
  ROUTE_NODB0(Get, "/api/v1/config/llm/local", ConfigController,
              getLlmLocalConfig);
  ROUTE_NODB(Put, "/api/v1/config/llm/local", ConfigController,
             setLlmLocalConfig,
             "PUT /api/v1/config/llm/local \r\n\r\n" + req.body);

  // Debug
  ROUTE_NODB(Post, "/api/v1/debug/enable", DebugController, setEnabled,
             "POST /api/v1/debug/enable \r\n\r\n" + req.body);
  ROUTE_NODB(Post, "/api/v1/debug/breakpoint", DebugController, setBreakpoint,
             "POST /api/v1/debug/breakpoint \r\n\r\n" + req.body);
  ROUTE_NODB(Delete, "/api/v1/debug/breakpoint", DebugController,
             removeBreakpoint,
             "DELETE /api/v1/debug/breakpoint \r\n\r\n" + req.body);
  ROUTE_NODB0(Get, "/api/v1/debug/breakpoints", DebugController,
              listBreakpoints);
  ROUTE_NODB(Post, R"(/api/v1/debug/([a-zA-Z0-9\-_]+)/continue)",
             DebugController, doContinue,
             "POST /api/v1/debug/" + req.matches[1].str() +
                 "/continue \r\n\r\n" + req.body);
  ROUTE_NODB(Post, R"(/api/v1/debug/([a-zA-Z0-9\-_]+)/step_over)",
             DebugController, doStepOver,
             "POST /api/v1/debug/" + req.matches[1].str() +
                 "/step_over \r\n\r\n" + req.body);
  ROUTE_NODB(Post, R"(/api/v1/debug/([a-zA-Z0-9\-_]+)/skip)", DebugController,
             doSkip,
             "POST /api/v1/debug/" + req.matches[1].str() + "/skip \r\n\r\n" +
                 req.body);
  ROUTE_NODB(Post, R"(/api/v1/debug/([a-zA-Z0-9\-_]+)/modify_args)",
             DebugController, modifyArguments,
             "POST /api/v1/debug/" + req.matches[1].str() +
                 "/modify_args \r\n\r\n" + req.body);
  ROUTE_NODB(Post, R"(/api/v1/debug/([a-zA-Z0-9\-_]+)/modify_result)",
             DebugController, modifyResult,
             "POST /api/v1/debug/" + req.matches[1].str() +
                 "/modify_result \r\n\r\n" + req.body);
  ROUTE_NODB(Get, R"(/api/v1/debug/([a-zA-Z0-9\-_]+))", DebugController,
             getState, "GET /api/v1/debug/" + req.matches[1].str());

#undef ROUTE
#undef ROUTE0
#undef ROUTE_NODB
#undef ROUTE_NODB0

  // 404
  svr.set_error_handler(
      [setCors](const httplib::Request &req, httplib::Response &res) {
        setCors(req, res);
        if (res.body.empty()) {
          res.set_content(not_found_json(), "application/json");
        }
      });

  std::cout << "Listening: " << cfg.host << ":" << cfg.port << "\n";
  std::cout << "Health endpoint: GET /api/v1/health\n";

  std::thread listenThread([&]() { svr.listen(cfg.host.c_str(), cfg.port); });

  while (running.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
  }

  svr.stop();
  if (listenThread.joinable()) {
    listenThread.join();
  }

  return 0;
}

std::string HttpServer::healthResponse() const {
  std::ostringstream body;
  body
      << R"({"success":true,"data":{"status":"ok","service":"codepilot-agent-server","version":"0.1.0","database":{"type":"sqlite","connected":)"
      << (config_.databaseConnected ? "true" : "false") << R"(,"path":")"
      << json_escape(config_.databasePath) << R"(")";
  if (!config_.databaseError.empty()) {
    body << R"(,"error":")" << json_escape(config_.databaseError) << R"(")";
  }
  body << "}}}";
  return body.str();
}

} // namespace codepilot
