#include "MessageBus.h"
#include <cctype>
#include <stdexcept>

namespace codepilot {

void MessageBus::skipWhitespace(const std::string &text, size_t &pos) {
  while (pos < text.size() &&
         std::isspace(static_cast<unsigned char>(text[pos]))) {
    ++pos;
  }
}

std::string MessageBus::readUntil(const std::string &text, size_t &pos,
                                  char delimiter) {
  size_t start = pos;
  while (pos < text.size() && text[pos] != delimiter) {
    ++pos;
  }
  std::string result = text.substr(start, pos - start);
  if (pos < text.size())
    ++pos;
  return result;
}

json MessageBus::parseAttributes(const std::string &text, size_t &pos) {
  skipWhitespace(text, pos);
  if (pos >= text.size() || text[pos] != '{') {
    return json::object();
  }
  size_t start = pos;
  int braceDepth = 0;
  bool inString = false;
  while (pos < text.size()) {
    char ch = text[pos];
    if (ch == '"' && (pos == start || text[pos - 1] != '\\')) {
      inString = !inString;
    }
    if (!inString) {
      if (ch == '{')
        ++braceDepth;
      else if (ch == '}') {
        --braceDepth;
        if (braceDepth == 0) {
          ++pos;
          return json::parse(text.substr(start, pos - start), nullptr, false);
        }
      }
    }
    ++pos;
  }
  return json::object();
}

static json coerceNumeric(const std::string &s) {
  try {
    if (s.find('.') != std::string::npos) {
      return std::stod(s);
    } else {
      return static_cast<int64_t>(std::stoll(s));
    }
  } catch (...) {
    return s;
  }
}

ParsedTag MessageBus::parseSingleTag(const std::string &text, size_t &pos) {
  ParsedTag tag;

  if (pos >= text.size() || text[pos] != '<')
    return tag;
  ++pos;

  skipWhitespace(text, pos);
  tag.tagName = readUntil(text, pos, '>');

  if (!tag.tagName.empty() && tag.tagName.back() == '/') {
    tag.tagName.pop_back();
    while (!tag.tagName.empty() &&
           std::isspace(static_cast<unsigned char>(tag.tagName.back()))) {
      tag.tagName.pop_back();
    }
    return tag;
  }

  // ── 属性解析：支持 key="value" 和 JSON {...} ──
  size_t spacePos = tag.tagName.find(' ');
  if (spacePos != std::string::npos) {
    std::string attrStr = tag.tagName.substr(spacePos + 1);
    tag.tagName = tag.tagName.substr(0, spacePos);

    if (attrStr.find('{') != std::string::npos) {
      tag.attributes = json::parse(attrStr, nullptr, false);
    } else {
      json attrs = json::object();
      size_t ap = 0;
      while (ap < attrStr.size()) {
        skipWhitespace(attrStr, ap);
        if (ap >= attrStr.size())
          break;

        size_t keyStart = ap;
        while (ap < attrStr.size() && attrStr[ap] != '=' &&
               !std::isspace(static_cast<unsigned char>(attrStr[ap]))) {
          ++ap;
        }
        std::string key = attrStr.substr(keyStart, ap - keyStart);
        skipWhitespace(attrStr, ap);
        if (ap >= attrStr.size() || attrStr[ap] != '=')
          break;
        ++ap;
        skipWhitespace(attrStr, ap);

        if (ap < attrStr.size() && attrStr[ap] == '"') {
          ++ap;
          size_t valStart = ap;
          while (ap < attrStr.size() && attrStr[ap] != '"')
            ++ap;
          std::string value = attrStr.substr(valStart, ap - valStart);
          if (ap < attrStr.size())
            ++ap;
          attrs[key] = coerceNumeric(value);
        } else {
          size_t valStart = ap;
          while (ap < attrStr.size() &&
                 !std::isspace(static_cast<unsigned char>(attrStr[ap]))) {
            ++ap;
          }
          std::string value = attrStr.substr(valStart, ap - valStart);
          attrs[key] = coerceNumeric(value);
        }
      }
      if (!attrs.empty()) {
        tag.attributes = attrs;
      }
    }
  }

  // ── 读内容 ──
  std::string closeTag = "</" + tag.tagName + ">";
  size_t closePos = text.find(closeTag, pos);
  if (closePos == std::string::npos) {
    tag.content = text.substr(pos);
    pos = text.size();
    return tag;
  }

  tag.content = text.substr(pos, closePos - pos);

  // ── 递归解析子标签 ──
  size_t childPos = 0;
  while (childPos < tag.content.size()) {
    size_t openPos = tag.content.find('<', childPos);
    if (openPos == std::string::npos)
      break;

    if (openPos + 1 < tag.content.size() && tag.content[openPos + 1] == '/') {
      childPos = openPos + 1;
      continue;
    }

    size_t tagNameEnd = tag.content.find_first_of(" >/", openPos + 1);
    if (tagNameEnd == std::string::npos)
      break;

    std::string childName =
        tag.content.substr(openPos + 1, tagNameEnd - openPos - 1);
    size_t childClose = tag.content.find("</" + childName + ">", openPos);
    if (childClose == std::string::npos) {
      childPos = openPos + 1;
      continue;
    }

    size_t savePos = childPos;
    childPos = openPos;
    ParsedTag child = parseSingleTag(tag.content, childPos);
    if (!child.tagName.empty()) {
      tag.children.push_back(child);
    } else {
      childPos = savePos + 1;
    }
  }

  pos = closePos + closeTag.size();
  return tag;
}

TagCollection MessageBus::parse(const std::string &text) {
  TagCollection result;
  size_t pos = 0;

  while (pos < text.size()) {
    size_t openPos = text.find('<', pos);
    if (openPos == std::string::npos)
      break;

    if (openPos + 1 < text.size() && text[openPos + 1] == '/') {
      pos = openPos + 1;
      continue;
    }

    pos = openPos;
    ParsedTag tag = parseSingleTag(text, pos);
    if (!tag.tagName.empty()) {
      result.groups[tag.tagName].push_back(tag);
    }
  }

  return result;
}

std::string MessageBus::extractTagContent(const std::string &text,
                                          const std::string &tagName) {
  TagCollection tags = parse(text);
  auto it = tags.groups.find(tagName);
  if (it != tags.groups.end() && !it->second.empty()) {
    return it->second.front().content;
  }
  return "";
}

bool MessageBus::hasTag(const std::string &text, const std::string &tagName) {
  TagCollection tags = parse(text);
  return tags.has(tagName);
}

} // namespace codepilot