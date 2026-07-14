#pragma once

#include <map>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>


namespace codepilot {

using json = nlohmann::json;

// ============================================================
// ParsedTag — 解析后的单个标签
// ============================================================
struct ParsedTag {
  std::string tagName; // "cmd", "plan", "done", "fail", "ask", "read", "write"
  std::string content; // 标签内文本
  json attributes;     // 标签属性（JSON 格式）
  std::vector<ParsedTag>
      children; // 子标签（用于 <plan> 内嵌 <add>/<complete> 等）
};

// ============================================================
// TagCollection — 按标签名分组的解析结果
// ============================================================
struct TagCollection {
  // 按标签名分组存储
  std::map<std::string, std::vector<ParsedTag>> groups;

  // 获取某标签名的所有实例
  std::vector<ParsedTag> get(const std::string &tagName) const {
    auto it = groups.find(tagName);
    if (it != groups.end())
      return it->second;
    return {};
  }

  // 检查是否存在某标签
  bool has(const std::string &tagName) const {
    auto it = groups.find(tagName);
    return it != groups.end() && !it->second.empty();
  }

  // 获取第一个某标签实例
  ParsedTag getFirst(const std::string &tagName) const {
    auto it = groups.find(tagName);
    if (it != groups.end() && !it->second.empty()) {
      return it->second.front();
    }
    return {};
  }

  // 获取所有标签名
  std::vector<std::string> tagNames() const {
    std::vector<std::string> names;
    for (const auto &[name, _] : groups) {
      names.push_back(name);
    }
    return names;
  }
};

// ============================================================
// MessageBus — 通用标签解析器
// 从 LLM 原始输出中解析 XML 标签，替代旧的 ResponseParser
// ============================================================
class MessageBus {
public:
  // 解析 LLM 原始输出文本中的所有 XML 标签
  static TagCollection parse(const std::string &text);

  // 提取指定标签的内容（不含标签名和属性）
  static std::string extractTagContent(const std::string &text,
                                       const std::string &tagName);

  // 检查文本中是否存在指定标签
  static bool hasTag(const std::string &text, const std::string &tagName);

private:
  // 递归解析标签（支持嵌套子标签）
  static ParsedTag parseSingleTag(const std::string &text, size_t &pos);

  // 解析标签属性（JSON 格式：{...}）
  static json parseAttributes(const std::string &text, size_t &pos);

  // 跳过空白
  static void skipWhitespace(const std::string &text, size_t &pos);

  // 读取直到指定字符
  static std::string readUntil(const std::string &text, size_t &pos,
                               char delimiter);
};

} // namespace codepilot