// xml_protocol.cpp — Protocol definitions for skill-agent communication
#include "xml_protocol.hpp"
#include <regex>

namespace astral {

bool XmlProtocol::parse_plan(const std::string &output, PlanResult &plan) {
  std::smatch m;
  std::string plan_re = "<plan>([\\s\\S]*?)</plan>";
  if (!std::regex_search(output, m, std::regex(plan_re)))
    return false;

  std::string inner = m[1].str();
  std::string item_re =
      "<item\\s+skill=\"([^\"]+)\"\\s+task=\"([^\"]*)\"\\s*/>";
  std::regex item_r(item_re);
  std::sregex_iterator it(inner.begin(), inner.end(), item_r);
  std::sregex_iterator end;
  for (; it != end; ++it) {
    TaskItem ti;
    ti.skill = (*it)[1].str();
    ti.task = (*it)[2].str();
    ti.fallback = false;
    plan.tasks.push_back(ti);
  }
  return !plan.tasks.empty();
}

bool XmlProtocol::parse_cmd(const std::string &output, std::string &cmd) {
  std::smatch m;
  std::string cmd_re = "<cmd>([\\s\\S]*?)</cmd>";
  if (std::regex_search(output, m, std::regex(cmd_re))) {
    cmd = m[1].str();
    return true;
  }
  return false;
}

bool XmlProtocol::has_done(const std::string &output, std::string &summary) {
  std::smatch m;
  if (std::regex_search(output, m, std::regex("DONE\\s*(.*)"))) {
    summary = m[1].str();
    return true;
  }
  return false;
}

int XmlProtocol::has_final_marker(const std::string &output,
                                  std::string &message) {
  // Check FAIL first: FAIL marker at start of a line
  // We search for newline+whitespace+FAIL or FAIL at start of string
  std::smatch m;
  // FAIL: must appear at start of string or after newline, capture rest of line
  if (std::regex_search(output, m,
                        std::regex("(?:^|\\n)\\s*FAIL\\b\\s*([^\\r\\n]*)"))) {
    message = m[1].str();
    return 2;
  }
  // DONE: must appear at start of string or after newline, capture rest of line
  if (std::regex_search(output, m,
                        std::regex("(?:^|\\n)\\s*DONE\\b\\s*([^\\r\\n]*)"))) {
    message = m[1].str();
    return 1;
  }
  return 0;
}

bool XmlProtocol::has_chat(const std::string &output, std::string &content) {
  std::smatch m;
  if (std::regex_search(output, m, std::regex("<chat>([\\s\\S]*?)</chat>"))) {
    content = m[1].str();
    return true;
  }
  return false;
}

} // namespace astral