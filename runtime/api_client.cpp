// api_client.cpp — HTTP-based AI API client for LLM calls
#include "api_client.hpp"
#include "../core/agent.hpp" // for AgentConfig
#include "../core/util.hpp"
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace astral {

ApiClient::ApiClient(const AgentConfig &cfg) : cfg_(cfg) {}

std::string ApiClient::build_body(const std::string &system_prompt,
                                  const std::string &user_input,
                                  double temperature) const {
  std::string body;
  body += "{\"model\":\"";
  body += util::json_escape(cfg_.model);
  body += "\",\"messages\":[{\"role\":\"system\",\"content\":\"";
  body += util::json_escape(system_prompt);
  body += "\"},{\"role\":\"user\",\"content\":\"";
  body += util::json_escape(user_input);
  body += "\"}],\"temperature\":";
  body += std::to_string(temperature);
  body += ",\"max_tokens\":";
  body += std::to_string(cfg_.max_tokens);
  body += "}";
  return body;
}

std::string ApiClient::http_post(const std::string &url,
                                 const std::string &body) const {
  std::string in_file = "astral_req_" + std::to_string(rand()) + ".json";
  std::string out_file = "astral_resp_" + std::to_string(rand()) + ".json";
  {
    std::ofstream f(in_file, std::ios::binary);
    f.write(body.data(), body.size());
  }

  std::string cmd;
#ifdef _WIN32
  cmd = "curl.exe -s -X POST \"" + url +
#else
  cmd = "curl -s -X POST \"" + url +
#endif
        "\" -H \"Content-Type: application/json\""
        " -H \"Authorization: Bearer " +
        cfg_.api_key + "\" -d \"@" + in_file + "\" -o \"" + out_file +
        "\" 2>nul";

  int ret = system(cmd.c_str());
  remove(in_file.c_str());

  std::ifstream f(out_file, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  f.close();
  remove(out_file.c_str());
  std::string result = ss.str();

  if (result.empty() && ret != 0) {
    result = "{\"error\":\"HTTP request failed (exit code " +
             std::to_string(ret) + ")\"}";
  }
  return result;
}

std::string ApiClient::parse_content(const std::string &resp) const {
  if (resp.find("\"error\"") != std::string::npos) {
    auto ep = resp.find("\"message\"");
    if (ep == std::string::npos)
      ep = resp.find("Authentication");
    std::string err = (ep != std::string::npos) ? resp.substr(ep) : resp;
    if (err.size() > 120)
      err = err.substr(0, 120);
    return "[API Error: " + err + "]";
  }

  const char *patterns[] = {"\"content\":\"", "\"content\": \""};
  size_t start = std::string::npos;
  for (auto pat : patterns) {
    start = resp.find(pat);
    if (start != std::string::npos) {
      start += strlen(pat);
      break;
    }
  }
  if (start == std::string::npos) {
    std::string preview = resp.substr(0, std::min<size_t>(resp.size(), 200));
    return "[API: " + preview + "]";
  }

  // Find the closing quote of the content field (stopping at ",\"logprobs\" or
  // "}\n")
  std::string content;
  for (size_t i = start; i < resp.size(); i++) {
    if (resp[i] == '\\' && i + 1 < resp.size()) {
      switch (resp[i + 1]) {
      case 'n':
        content += '\n';
        i++;
        break;
      case 't':
        content += '\t';
        i++;
        break;
      case 'r':
        content += '\r';
        i++;
        break;
      case '"':
        content += '"';
        i++;
        break;
      case '\\':
        content += '\\';
        i++;
        break;
      default:
        content += resp[i];
        break;
      }
    } else if (resp[i] == '"')
      break;
    else
      content += resp[i];
  }
  return content;
}

void ApiClient::extract_tokens(const std::string &raw_json, int &prompt_tokens,
                               int &completion_tokens, int &total_tokens) {
  prompt_tokens = 0;
  completion_tokens = 0;
  total_tokens = 0;

  auto fp = raw_json.find("\"prompt_tokens\"");
  if (fp != std::string::npos) {
    auto colon = raw_json.find(':', fp);
    if (colon != std::string::npos) {
      auto end = raw_json.find_first_of(",}\n", colon + 1);
      if (end != std::string::npos)
        prompt_tokens = std::stoi(raw_json.substr(colon + 1, end - colon - 1));
    }
  }

  auto fc = raw_json.find("\"completion_tokens\"");
  if (fc != std::string::npos) {
    auto colon = raw_json.find(':', fc);
    if (colon != std::string::npos) {
      auto end = raw_json.find_first_of(",}\n", colon + 1);
      if (end != std::string::npos)
        completion_tokens =
            std::stoi(raw_json.substr(colon + 1, end - colon - 1));
    }
  }

  auto ft = raw_json.find("\"total_tokens\"");
  if (ft != std::string::npos) {
    auto colon = raw_json.find(':', ft);
    if (colon != std::string::npos) {
      auto end = raw_json.find_first_of(",}\n", colon + 1);
      if (end != std::string::npos)
        total_tokens = std::stoi(raw_json.substr(colon + 1, end - colon - 1));
    }
  }
}

ChatResult ApiClient::chat(const std::string &system_prompt,
                           const std::string &user_input,
                           double temperature) const {
  std::string body = build_body(system_prompt, user_input, temperature);
  std::string resp = http_post(cfg_.api_url + "/chat/completions", body);
  ChatResult cr;
  cr.raw_json = resp;
  cr.content = parse_content(resp);
  return cr;
}

} // namespace astral