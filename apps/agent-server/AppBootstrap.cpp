#include "api/HttpServer.h"
#include "application/ToolSystem.h"
#include "infrastructure/storage/repositories/PermissionRepository.h"
#include "infrastructure/storage/repositories/ToolCallRepository.h"

#include "common/logging/Logger.h"

#include <sqlite3.h>

#include <atomic>
#include <csignal>
#include <cstdio>
#include <exception>
#include <iostream>
#include <string>

namespace {

std::atomic_bool running{true};

void handle_signal(int) { running.store(false); }

std::string extract_storage_path(const std::string &config_path) {
  FILE *file = std::fopen(config_path.c_str(), "rb");
  if (!file) {
    return "/data/agent.db";
  }

  std::string content;
  char buffer[4096] = {};
  while (const std::size_t read = std::fread(buffer, 1, sizeof(buffer), file)) {
    content.append(buffer, read);
  }
  std::fclose(file);

  const std::string marker = R"("path")";
  const std::size_t marker_pos = content.find(marker);
  if (marker_pos == std::string::npos) {
    return "/data/agent.db";
  }

  const std::size_t colon_pos = content.find(':', marker_pos + marker.size());
  const std::size_t first_quote = content.find('"', colon_pos);
  const std::size_t second_quote = content.find('"', first_quote + 1);
  if (colon_pos == std::string::npos || first_quote == std::string::npos ||
      second_quote == std::string::npos) {
    return "/data/agent.db";
  }

  return content.substr(first_quote + 1, second_quote - first_quote - 1);
}

std::string extract_workspace_root(const std::string &config_path) {
  FILE *file = std::fopen(config_path.c_str(), "rb");
  if (!file) {
    return "/workspace";
  }

  std::string content;
  char buffer[4096] = {};
  while (const std::size_t read = std::fread(buffer, 1, sizeof(buffer), file)) {
    content.append(buffer, read);
  }
  std::fclose(file);

  const std::string workspace_marker = R"("workspace")";
  const std::size_t workspace_pos = content.find(workspace_marker);
  if (workspace_pos == std::string::npos) {
    return "/workspace";
  }

  const std::string root_marker = R"("root")";
  const std::size_t root_pos =
      content.find(root_marker, workspace_pos + workspace_marker.size());
  if (root_pos == std::string::npos) {
    return "/workspace";
  }

  const std::size_t colon_pos =
      content.find(':', root_pos + root_marker.size());
  const std::size_t first_quote = content.find('"', colon_pos);
  const std::size_t second_quote = content.find('"', first_quote + 1);
  if (colon_pos == std::string::npos || first_quote == std::string::npos ||
      second_quote == std::string::npos) {
    return "/workspace";
  }

  return content.substr(first_quote + 1, second_quote - first_quote - 1);
}

bool initialize_database(const std::string &path, std::string &error) {
  sqlite3 *db = nullptr;
  if (sqlite3_open(path.c_str(), &db) != SQLITE_OK) {
    error = db ? sqlite3_errmsg(db) : "sqlite3_open failed";
    if (db) {
      sqlite3_close(db);
    }
    return false;
  }

  const char *schema = R"SQL(
CREATE TABLE IF NOT EXISTS system_health (
    id INTEGER PRIMARY KEY CHECK (id = 1),
    service TEXT NOT NULL,
    checked_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);
INSERT INTO system_health (id, service, checked_at)
VALUES (1, 'codepilot-agent-server', CURRENT_TIMESTAMP)
ON CONFLICT(id) DO UPDATE SET checked_at = CURRENT_TIMESTAMP;

CREATE TABLE IF NOT EXISTS sessions (
    id TEXT PRIMARY KEY,
    title TEXT NOT NULL,
    created_at TEXT NOT NULL,
    updated_at TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS tasks (
    id TEXT PRIMARY KEY,
    session_id TEXT NOT NULL,
    workspace_id TEXT NOT NULL,
    goal TEXT NOT NULL,
    status TEXT NOT NULL,
    plan TEXT,
    current_step TEXT,
    created_at TEXT NOT NULL,
    updated_at TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS chat_messages (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    prompt TEXT NOT NULL,
    response TEXT NOT NULL,
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS workspaces (
    id TEXT PRIMARY KEY,
    name TEXT NOT NULL,
    path TEXT NOT NULL,
    created_at TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS execution_logs (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    task_id TEXT NOT NULL,
    type TEXT NOT NULL,
    content TEXT NOT NULL,
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);
)SQL";

  char *error_message = nullptr;
  const int exec_result =
      sqlite3_exec(db, schema, nullptr, nullptr, &error_message);
  if (exec_result != SQLITE_OK) {
    error = error_message ? error_message : "schema initialization failed";
    sqlite3_free(error_message);
    sqlite3_close(db);
    return false;
  }

  try {
    PermissionRepository(db).initTable();
    ToolCallRepository(db).initTable();
  } catch (const std::exception &init_error) {
    error = init_error.what();
    sqlite3_close(db);
    return false;
  }

  sqlite3_stmt *stmt = nullptr;
  const int prepare_result = sqlite3_prepare_v2(
      db, "SELECT COUNT(*) FROM system_health;", -1, &stmt, nullptr);
  if (prepare_result != SQLITE_OK || sqlite3_step(stmt) != SQLITE_ROW) {
    error = sqlite3_errmsg(db);
    if (stmt) {
      sqlite3_finalize(stmt);
    }
    sqlite3_close(db);
    return false;
  }

  sqlite3_finalize(stmt);
  sqlite3_close(db);
  error.clear();
  return true;
}

} // namespace

int run_agent_server(const std::string &config_path) {
  std::signal(SIGINT, handle_signal);
  std::signal(SIGTERM, handle_signal);

  // 初始化统一日志系统
  // 优先尝试从配置加载，若文件不存在则使用默认配置
  const std::string logConfigPath = "./config/logging.json";
  codepilot::log::initFromFile(logConfigPath);

  const std::string database_path = extract_storage_path(config_path);
  const std::string workspace_root = extract_workspace_root(config_path);
  std::string database_error;
  const bool database_connected =
      initialize_database(database_path, database_error);
  codepilot::ToolSystem::getInstance().init(workspace_root, config_path);

  LOG_INFO("CodePilot Agent Server starting");
  LOG_INFO("Config: {}", config_path);
  LOG_INFO("Workspace: {}", workspace_root);
  LOG_INFO("SQLite: {} connected={}", database_path,
           (database_connected ? "true" : "false"));
  if (!database_error.empty()) {
    LOG_ERROR("SQLite error: {}", database_error);
  }

  codepilot::HttpServerConfig server_config;
  server_config.host = "0.0.0.0";
  server_config.port = 8080;
  server_config.databasePath = database_path;
  server_config.databaseConnected = database_connected;
  server_config.databaseError = database_error;

  codepilot::HttpServer server(server_config);
  const int result = server.run(running);
  LOG_INFO("CodePilot Agent Server stopped");
  codepilot::log::shutdown();
  return result;
}
