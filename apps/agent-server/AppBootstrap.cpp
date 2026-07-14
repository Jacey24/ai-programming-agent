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

std::string extractConfigValue(const std::string &configPath,
                               const std::string &key) {
  const std::string raw = readFile(configPath);
  if (raw.empty()) {
    return "";
  }
  try {
    nlohmann::json config = nlohmann::json::parse(raw);
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
  // 相对路径直接返回（sqlite3 相对于 CWD 解析）
  return path.empty() ? "./storage/agent.db" : path;
}

std::string extract_workspace_root(const std::string &config_path) {
  const std::string root = extractConfigValue(config_path, "workspace.root");
  return root.empty() ? "./workspace" : root;
}

} // namespace

int run_agent_server(const std::string &config_path) {
  std::signal(SIGINT, handle_signal);
#ifndef _WIN32
  std::signal(SIGTERM, handle_signal);
#endif

  // 初始化统一日志系统
  const std::string logConfigPath = "./config/logging.json";
  codepilot::log::initFromFile(logConfigPath);

  const std::string database_path = extract_storage_path(config_path);
  const std::string workspace_root = extract_workspace_root(config_path);

  // 初始化工具系统
  codepilot::ToolSystem::getInstance().init(workspace_root, config_path);

  // 初始化存储门面（建表+system_health 统一由此完成，消除 AppBootstrap 双连接）
  bool database_connected = false;
  std::string database_error;
  try {
    codepilot::DataAccessFacade::getInstance().init(database_path);
    database_connected = true;
  } catch (const std::exception &e) {
    database_error = e.what();
  }

  // 初始化通信门面（绑定 EventBus + DataAccessFacade）
  codepilot::SSEGateway::getInstance().init(
      &codepilot::ToolSystem::getInstance().eventBus(),
      &codepilot::DataAccessFacade::getInstance());

  // 初始化 LLM 门面（加载 llm.json + llm.local.json）
  codepilot::LlmClientFacade::getInstance().init("config/llm.json");
  LOG_INFO("LlmClientFacade available: {}",
           codepilot::LlmClientFacade::getInstance().isAvailable() ? "true"
                                                                   : "false");
  LOG_INFO("LlmClientFacade default provider: {}",
           codepilot::LlmClientFacade::getInstance().getDefaultProvider());

  // 初始化 Agent 编排器（加载 Expert 配置，启用 Expert Chain 主循环）
  codepilot::AgentOrchestrator::getInstance().init("config/experts.json");
  LOG_INFO("AgentOrchestrator ready: {}",
           codepilot::AgentOrchestrator::getInstance().isReady() ? "true"
                                                                 : "false");
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