#include "api/controllers/ConfigController.h"
#include "facade/LlmClientFacade.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>

using json = nlohmann::json;
using namespace codepilot;

namespace {

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

void writeJson(const std::filesystem::path &path, const json &value) {
  std::ofstream output(path, std::ios::trunc);
  require(output.is_open(), "failed to open test config");
  output << value.dump(2);
}

json readJson(const std::filesystem::path &path) {
  std::ifstream input(path);
  return json::parse(input);
}

std::string request(const std::string &method, const std::string &path,
                    const json &body = json::object()) {
  return method + " " + path + " HTTP/1.1\r\nContent-Type: application/json\r\n\r\n" +
         body.dump();
}

json responseBody(const std::string &response) {
  const auto separator = response.find("\r\n\r\n");
  require(separator != std::string::npos, "invalid HTTP response");
  return json::parse(response.substr(separator + 4));
}

bool hasRuntimeProvider(const std::string &id) {
  for (const auto &provider : LlmClientFacade::getInstance().listProviders()) {
    if (provider == id)
      return true;
  }
  return false;
}

} // namespace

int main(int argc, char **argv) {
  if (argc == 2 && std::string(argv[1]) == "--live-deepseek") {
    auto &facade = LlmClientFacade::getInstance();
    facade.init("config/llm.json");
    const auto success = facade.testConnection(
        "deepseek", "https://api.deepseek.com", "deepseek-chat", "",
        "Reply with exactly OK");
    const auto invalidKey = facade.testConnection(
        "deepseek", "https://api.deepseek.com", "deepseek-chat",
        "invalid-test-key", "Reply OK");
    const auto invalidModel = facade.testConnection(
        "deepseek", "https://api.deepseek.com", "model-that-does-not-exist",
        "", "Reply OK");
    const auto invalidUrl = facade.testConnection(
        "deepseek", "http://127.0.0.1:1/v1", "deepseek-chat",
        "invalid-test-key", "Reply OK");
    std::cout << "success=" << success.success
              << " status=" << success.httpStatus << '\n'
              << "invalid_key_kind=" << static_cast<int>(invalidKey.errorKind)
              << " status=" << invalidKey.httpStatus << '\n'
              << "invalid_model_kind="
              << static_cast<int>(invalidModel.errorKind)
              << " status=" << invalidModel.httpStatus << '\n'
              << "invalid_url_kind=" << static_cast<int>(invalidUrl.errorKind)
              << " status=" << invalidUrl.httpStatus << '\n';
    return success.success && invalidKey.httpStatus == 401 &&
                   invalidModel.httpStatus == 400 &&
                   invalidUrl.errorKind == LlmErrorKind::Transport
               ? 0
               : 1;
  }

  const auto originalDirectory = std::filesystem::current_path();
  const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto runtime = std::filesystem::temp_directory_path() /
                       ("codepilot-llm-provider-test-" + std::to_string(unique));
  try {
    std::filesystem::create_directories(runtime / "config");
    writeJson(runtime / "config/llm.json",
              {{"default", "alpha"},
               {"providers",
                {{"alpha",
                  {{"name", "Alpha"},
                   {"base_url", "https://alpha.invalid/v1"},
                   {"model", "alpha-model"},
                   {"timeout_seconds", 1}}}}}});
    writeJson(runtime / "config/llm.local.json",
              {{"providers", {{"alpha", {{"api_key", "alpha-secret"}}}}}});
    std::filesystem::current_path(runtime);

    auto &facade = LlmClientFacade::getInstance();
    facade.init("config/llm.json");
    ConfigController controller;

    auto added = responseBody(controller.addLlmProvider(request(
        "POST", "/api/v1/config/llm/providers",
        {{"id", "beta"},
         {"base_url", "https://beta.invalid/v1"},
         {"model", "beta-model"},
         {"api_key", "beta-secret"}})));
    require(added.value("success", false), "provider add failed");
    require(hasRuntimeProvider("beta"), "added provider was not hot loaded");

    auto local = readJson("config/llm.local.json");
    require(local["providers"]["alpha"]["api_key"] == "alpha-secret",
            "adding beta overwrote alpha key");
    require(local["providers"]["beta"]["api_key"] == "beta-secret",
            "beta key was not stored independently");

    auto maskedUpdate = responseBody(controller.updateLlmProvider(request(
        "PUT", "/api/v1/config/llm/providers/alpha",
        {{"id", "alpha"},
         {"base_url", "https://alpha.invalid/v2"},
         {"model", "alpha-model-2"},
         {"api_key", "******"}})));
    require(maskedUpdate.value("success", false), "masked update failed");
    local = readJson("config/llm.local.json");
    require(local["providers"]["alpha"]["api_key"] == "alpha-secret",
            "mask overwrote alpha key");

    responseBody(controller.updateLlmProvider(request(
        "PUT", "/api/v1/config/llm/providers/alpha",
        {{"id", "alpha"},
         {"base_url", "https://alpha.invalid/v2"},
         {"model", "alpha-model-2"},
         {"api_key", ""}})));
    local = readJson("config/llm.local.json");
    require(local["providers"]["alpha"]["api_key"] == "alpha-secret",
            "empty key overwrote alpha key");

    responseBody(controller.updateLlmProvider(request(
        "PUT", "/api/v1/config/llm/providers/alpha",
        {{"id", "alpha"},
         {"base_url", "https://alpha.invalid/v2"},
         {"model", "alpha-model-2"},
         {"api_key", "alpha-secret-2"}})));
    local = readJson("config/llm.local.json");
    require(local["providers"]["alpha"]["api_key"] == "alpha-secret-2",
            "new alpha key was not applied");
    require(local["providers"]["beta"]["api_key"] == "beta-secret",
            "updating alpha overwrote beta key");

    const auto listed = controller.listLlmProviders();
    require(listed.find("alpha-secret-2") == std::string::npos &&
                listed.find("beta-secret") == std::string::npos,
            "provider list exposed an API key");

    auto deleted = responseBody(controller.deleteLlmProvider(
        request("DELETE", "/api/v1/config/llm/providers/alpha")));
    require(deleted.value("success", false), "provider delete failed");
    require(!hasRuntimeProvider("alpha"), "deleted provider remained loaded");
    local = readJson("config/llm.local.json");
    require(!local["providers"].contains("alpha"),
            "deleted provider key remained stored");
    require(local["providers"]["beta"]["api_key"] == "beta-secret",
            "deleting alpha removed beta key");

    std::filesystem::current_path(originalDirectory);
    std::filesystem::remove_all(runtime);
    std::cout << "LLM provider config tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::filesystem::current_path(originalDirectory);
    std::error_code ignored;
    std::filesystem::remove_all(runtime, ignored);
    std::cerr << error.what() << '\n';
    return 1;
  }
}
