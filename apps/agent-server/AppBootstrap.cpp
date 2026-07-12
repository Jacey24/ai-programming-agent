#include "api/HttpServer.h"
#include "application/ToolSystem.h"
#include "facade/DataAccessFacade.h"
#include "facade/LlmClientFacade.h"
#include "facade/SSEGateway.h"
#include "infrastructure/storage/SqliteConnection.h"

#include "common/logging/Logger.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <csignal>
#include <cstdio>
#include <exception>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

std::atomic_bool running{true};

void handle_signal(int) { running.store(false); }

// ============================================================
// 统一 JSON 配置提取（替代原 80 行手写字符串扫描，第 5 点优化）
// ============================================================
std::string readFile(const std::string &path) {
  std::ifstream in(path);
  if (!in.is_open()) {
    return "";
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

std::string extractConfigValue(const std::string &configPath,
                               const std::string &key) {
  const std::string raw = readFile(configPath);
  if (raw.empty()) {
    return "";
  }
  try {
    nlohmann::json config = nlohmann::json::parse(raw);
    // 支持嵌套路径，如 "storage.path" 或 "workspace.root"
    const auto dot = key.find('.');
    if (dot != std::string::npos) {
      const std::string top = key.substr(0, dot);
      const std::string sub = key.substr(dot + 1);
      if (config.contains(top) && config[top].is_object() &&
          config[top].contains(sub)) {
        return config[top][sub].get<std::string>();
      }
    } else {
      if (config.contains(key)) {
        return config[key].get<std::string>();
      }
    }
  } catch (const std::exception &) {
    // 解析失败返回空，调用方使用默认值
  }
  return "";
}

std::string extract_storage_path(const std::string &config_path) {
  const std::string path = extractConfigValue(config_path, "storage.path");
  return path.empty() ? "/data/agent.db" : path;
}

std::string extract_workspace_root(const std::string &config_path) {
  const std::string root = extractConfigValue(config_path, "workspace.root");
  return root.empty() ? "/workspace" : root;
}

// ============================================================
// 数据库初始化（第 4 点优化：schema 由 DataAccessFacade 统一管理）
// ============================================================
bool initialize_database(const std::string &path, std::string &error) {
  sqlite3 *db = nullptr;
  if (codepilot::openSqliteConnection(path.c_str(), &db) != SQLITE_OK) {
    error = db ? sqlite3_errmsg(db) : "sqlite3_open failed";
    if (db) {
      sqlite3_close(db);
    }
    return false;
  }
  codepilot::configureSqliteDatabase(db);

  // 仅建健康检查表（8 张业务表由 DataAccessFacade::createAllTables 统管）
  const char *schema = R"SQL(
CREATE TABLE IF NOT EXISTS system_health (
    id INTEGER PRIMARY KEY CHECK (id = 1),
    service TEXT NOT NULL,
    checked_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);
INSERT INTO system_health (id, service, checked_at)
VALUES (1, 'codepilot-agent-server', CURRENT_TIMESTAMP)
ON CONFLICT(id) DO UPDATE SET checked_at = CURRENT_TIMESTAMP;
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
  const std::string logConfigPath = "./config/logging.json";
  codepilot::log::initFromFile(logConfigPath);

  const std::string database_path = extract_storage_path(config_path);
  const std::string workspace_root = extract_workspace_root(config_path);
  std::string database_error;
  const bool database_connected =
      initialize_database(database_path, database_error);
  codepilot::ToolSystem::getInstance().init(workspace_root, config_path);

  // 初始化存储门面
  codepilot::DataAccessFacade::getInstance().init(database_path);

  // 初始化通信门面（绑定 EventBus + DataAccessFacade）
  codepilot::SSEGateway::getInstance().init(
      &codepilot::ToolSystem::getInstance().eventBus(),
      &codepilot::DataAccessFacade::getInstance());

  // 初始化 LLM 门面（加载 llm.json + llm.local.json）
  codepilot::LlmClientFacade::getInstance().init("config/llm.json");

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