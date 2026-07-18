#include "InternalConsole.h"
#include "api/HttpServer.h"
#include "application/ToolSystem.h"
#include "domain/agent/AgentOrchestrator.h"
#include "facade/DataAccessFacade.h"
#include "facade/LlmClientFacade.h"
#include "facade/SSEGateway.h"

#include "common/logging/Logger.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <csignal>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

std::atomic_bool running{true};

void handle_signal(int) { running.store(false); }

// ============================================================
// 统一 JSON 配置提取
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

class LoggingShutdownGuard {
public:
  ~LoggingShutdownGuard() {
    if (active_) {
      codepilot::log::shutdown();
    }
  }
  void activate() { active_ = true; }

private:
  bool active_{false};
};

nlohmann::json loadRequiredConfig(const std::string &configPath) {
  std::ifstream input(configPath);
  if (!input.is_open()) {
    throw std::runtime_error("required configuration file is not readable: '" +
                             configPath + "'");
  }

  std::ostringstream contents;
  contents << input.rdbuf();
  try {
    auto config = nlohmann::json::parse(contents.str());
    if (!config.is_object()) {
      throw std::runtime_error("top-level JSON value must be an object");
    }
    return config;
  } catch (const std::exception &error) {
    throw std::runtime_error("invalid JSON in required configuration file '" +
                             configPath + "': " + error.what());
  }
}

std::string optionalString(const nlohmann::json &config,
                           const std::string &section, const std::string &key) {
  if (!config.contains(section)) {
    return "";
  }
  if (!config[section].is_object()) {
    throw std::runtime_error("configuration field '" + section +
                             "' must be an object");
  }
  if (!config[section].contains(key)) {
    return "";
  }
  if (!config[section][key].is_string()) {
    throw std::runtime_error("configuration field '" + section + "." + key +
                             "' must be a string");
  }
  return config[section][key].get<std::string>();
}

std::string extract_storage_path(const nlohmann::json &config) {
  const std::string path = optionalString(config, "storage", "path");
  return path.empty() ? "./storage/agent.db" : path;
}

std::string extract_workspace_root(const nlohmann::json &config) {
  const std::string root = optionalString(config, "workspace", "root");
  return root.empty() ? "./workspace" : root;
}

void ensureDirectory(const std::filesystem::path &path,
                     const std::string &component) {
  std::error_code error;
  const bool exists = std::filesystem::exists(path, error);
  if (error) {
    throw std::runtime_error("cannot inspect " + component + " directory '" +
                             path.string() + "': " + error.message());
  }
  if (exists) {
    if (!std::filesystem::is_directory(path, error) || error) {
      throw std::runtime_error(component + " path is not a directory: '" +
                               path.string() + "'");
    }
    return;
  }
  if (!std::filesystem::create_directories(path, error) || error) {
    throw std::runtime_error("failed to create " + component + " directory '" +
                             path.string() + "': " + error.message());
  }
}

void ensure_runtime_directories(const std::string &database_path,
                                const std::string &workspace_root) {
  const std::filesystem::path database_parent =
      std::filesystem::path(database_path).parent_path();
  if (!database_parent.empty()) {
    ensureDirectory(database_parent, "storage");
  }
  ensureDirectory(workspace_root, "workspace");
  ensureDirectory("./logs", "log");
}

} // namespace

int run_agent_server(const std::string &config_path, bool enableConsole) {
  running.store(true);
  std::signal(SIGINT, handle_signal);
#ifndef _WIN32
  std::signal(SIGTERM, handle_signal);
#endif

  nlohmann::json agent_config;
  try {
    agent_config = loadRequiredConfig(config_path);
  } catch (const std::exception &error) {
    std::cerr << "[FATAL][Configuration] " << error.what() << std::endl;
    return 2;
  }

  std::string database_path;
  std::string workspace_root;
  try {
    database_path = extract_storage_path(agent_config);
    workspace_root = extract_workspace_root(agent_config);
  } catch (const std::exception &error) {
    std::cerr << "[FATAL][Configuration] Invalid startup paths in '"
              << config_path << "': " << error.what() << std::endl;
    return 2;
  }

  try {
    ensure_runtime_directories(database_path, workspace_root);
  } catch (const std::exception &error) {
    std::cerr << "[ERROR] Unable to prepare runtime directories: "
              << error.what() << std::endl;
    return 1;
  }

  LoggingShutdownGuard logging_guard;
  const std::string logConfigPath = "./config/logging.json";
  try {
    loadRequiredConfig(logConfigPath);
    codepilot::log::initFromFile(logConfigPath);
    logging_guard.activate();
  } catch (const std::exception &error) {
    std::cerr << "[FATAL][Logging] Unable to initialize logging from '"
              << logConfigPath << "': " << error.what() << std::endl;
    return 3;
  }

  try {
    codepilot::ToolSystem::getInstance().init(workspace_root,
                                              "config/tools.json");
  } catch (const std::exception &error) {
    std::cerr << "[FATAL][ToolSystem] Initialization failed for workspace '"
              << workspace_root << "': " << error.what() << std::endl;
    return 4;
  }

  {
    auto &dataAccess = codepilot::DataAccessFacade::getInstance();
    bool dbOk = false;
    try {
      dataAccess.init(database_path);
      const auto recovery = dataAccess.recoverInterruptedTasks();
      LOG_INFO("Crash recovery complete: interrupted_tasks={}, "
               "expired_permissions={}, inserted_terminal_events={}",
               recovery.tasksInterrupted, recovery.permissionsExpired,
               recovery.terminalEventsInserted);
      dbOk = true;
    } catch (const std::exception &error) {
      std::cerr << "[WARN][SQLite] Database init/recovery failed on "
                << database_path << ": " << error.what()
                << " — attempting reset" << std::endl;
    }

    if (!dbOk) {
      // Delete the stale database and retry from scratch.
      // This is safe because the only local state is the chat history which
      // the user can rebuild.
      std::error_code ec;
      std::filesystem::remove(database_path, ec);
      // Also remove the WAL and journal files that SQLite may have created.
      std::filesystem::remove(database_path + "-wal", ec);
      std::filesystem::remove(database_path + "-shm", ec);
      std::filesystem::remove(database_path + "-journal", ec);

      try {
        dataAccess.init(database_path);
        const auto recovery = dataAccess.recoverInterruptedTasks();
        LOG_INFO("Database reset complete: interrupted_tasks={}, "
                 "expired_permissions={}, inserted_terminal_events={}",
                 recovery.tasksInterrupted, recovery.permissionsExpired,
                 recovery.terminalEventsInserted);
      } catch (const std::exception &error) {
        std::cerr << "[FATAL][SQLite] Unable to initialize database even after "
                     "reset '"
                  << database_path << "': " << error.what() << std::endl;
        return 5;
      }
    }
  }

  try {
    codepilot::SSEGateway::getInstance().init(
        &codepilot::ToolSystem::getInstance().eventBus(),
        &codepilot::DataAccessFacade::getInstance());
    if (!codepilot::SSEGateway::getInstance().isInitialized()) {
      throw std::runtime_error("gateway did not enter the initialized state");
    }
  } catch (const std::exception &error) {
    std::cerr << "[FATAL][SSEGateway] Initialization failed: " << error.what()
              << std::endl;
    return 4;
  }

  try {
    codepilot::LlmClientFacade::getInstance().init("config/llm.json");
  } catch (const std::exception &error) {
    std::cerr << "[WARN][LLM] Optional model configuration 'config/llm.json' "
                 "is unavailable: "
              << error.what() << std::endl;
  }
  LOG_INFO("LlmClientFacade available: {}",
           codepilot::LlmClientFacade::getInstance().isAvailable() ? "true"
                                                                   : "false");
  LOG_INFO("LlmClientFacade default provider: {}",
           codepilot::LlmClientFacade::getInstance().getDefaultProvider());

  try {
    codepilot::AgentOrchestrator::getInstance().init("config/experts.json");
  } catch (const std::exception &error) {
    std::cerr << "[FATAL][AgentOrchestrator] Initialization failed for "
                 "'config/experts.json': "
              << error.what() << std::endl;
    return 4;
  }
  LOG_INFO("AgentOrchestrator ready: {}",
           codepilot::AgentOrchestrator::getInstance().isReady() ? "true"
                                                                 : "false");
  if (!codepilot::AgentOrchestrator::getInstance().isReady()) {
    LOG_WARN("Optional expert configuration is unavailable; health and "
             "non-agent APIs remain available");
  }
  if (!codepilot::LlmClientFacade::getInstance().isAvailable()) {
    LOG_WARN("No LLM provider or API key is configured; model-dependent "
             "tasks will fail at runtime");
  }
  if (codepilot::AgentOrchestrator::getInstance().isReady()) {
    auto names = codepilot::AgentConfiguration::getInstance().listExpertNames();
    LOG_INFO("Loaded {} experts", names.size());
    for (const auto &name : names) {
      const auto *e =
          codepilot::AgentConfiguration::getInstance().getExpert(name);
      std::string tools;
      for (size_t i = 0; i < e->visibleTools.size(); ++i) {
        if (i > 0)
          tools += ",";
        tools += e->visibleTools[i];
      }
      LOG_INFO("  Expert [{}] entry={} llm={}/{} tools=[{}]", name,
               e->isEntry ? "yes" : "no",
               e->llmProvider.empty() ? "(default)" : e->llmProvider,
               e->llmModel.empty() ? "(default)" : e->llmModel,
               tools.empty() ? "(none)" : tools);
    }
  }

  LOG_INFO("CodePilot Agent Server starting");
  LOG_INFO("Config: {}", config_path);
  LOG_INFO("Workspace: {}", workspace_root);
  LOG_INFO("SQLite: {} connected={}", database_path, "true");

  codepilot::HttpServerConfig server_config;
  server_config.host = "0.0.0.0";
  server_config.port = 8080;
  server_config.databasePath = database_path;
  server_config.databaseConnected = true;

  codepilot::HttpServer server(server_config);
  std::unique_ptr<InternalConsole> console;
  if (enableConsole) {
    // 内建调试控制台（非阻塞 start）
    console = std::make_unique<InternalConsole>("localhost", 8080);
    console->start();
    LOG_INFO("InternalConsole started — use --console to access");
  }

  const int result = server.run(running);

  if (console) {
    console->stop();
  }

  LOG_INFO("CodePilot Agent Server stopped");
  return result;
}
