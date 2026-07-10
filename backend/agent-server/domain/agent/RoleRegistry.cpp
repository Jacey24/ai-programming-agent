#include "RoleRegistry.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace codepilot {

namespace {

std::string readFile(const std::string& path) {
    FILE* file = std::fopen(path.c_str(), "rb");
    if (!file) {
        return "";
    }
    std::string content;
    char buffer[4096] = {};
    while (std::size_t read = std::fread(buffer, 1, sizeof(buffer), file)) {
        content.append(buffer, read);
    }
    std::fclose(file);
    return content;
}

std::string extractString(const std::string& json, std::size_t& pos) {
    while (pos < json.size() && json[pos] != '"') {
        ++pos;
    }
    if (pos >= json.size()) return "";
    ++pos; // skip opening "
    std::string value;
    bool escaped = false;
    while (pos < json.size()) {
        char ch = json[pos];
        if (escaped) {
            value.push_back(ch);
            escaped = false;
            ++pos;
            continue;
        }
        if (ch == '\\') {
            escaped = true;
            ++pos;
            continue;
        }
        if (ch == '"') {
            ++pos;
            break;
        }
        value.push_back(ch);
        ++pos;
    }
    return value;
}

std::vector<std::string> extractStringArray(const std::string& json, std::size_t& pos) {
    std::vector<std::string> result;
    pos = json.find('[', pos);
    if (pos == std::string::npos) return result;
    ++pos;
    while (pos < json.size() && json[pos] != ']') {
        if (json[pos] == '"') {
            result.push_back(extractString(json, pos));
        } else {
            ++pos;
        }
    }
    if (pos < json.size() && json[pos] == ']') {
        ++pos;
    }
    return result;
}

} // namespace

bool RoleRegistry::loadFromFile(const std::string& configPath) {
    const std::string content = readFile(configPath);
    if (content.empty()) {
        return false;
    }

    std::size_t pos = content.find("\"roles\"");
    if (pos == std::string::npos) {
        return false;
    }
    pos = content.find('[', pos);
    if (pos == std::string::npos) {
        return false;
    }

    // Parse each role object
    int depth = 0;
    std::size_t objStart = std::string::npos;

    for (std::size_t i = pos + 1; i < content.size(); ++i) {
        if (content[i] == '{') {
            if (depth == 0) {
                objStart = i;
            }
            ++depth;
        } else if (content[i] == '}') {
            --depth;
            if (depth == 0 && objStart != std::string::npos) {
                std::string roleJson = content.substr(objStart, i - objStart + 1);

                RoleConfig role;
                std::size_t fieldPos = 0;

                // NOTE: find() returns the position of the KEY's opening quote.
                // extractString/extractStringArray must start scanning AFTER the
                // key literal, otherwise the key name itself is parsed as the value.
                fieldPos = roleJson.find("\"name\"", 0);
                if (fieldPos != std::string::npos) {
                    fieldPos += std::strlen("\"name\"");
                    role.name = extractString(roleJson, fieldPos);
                }

                fieldPos = roleJson.find("\"description\"", 0);
                if (fieldPos != std::string::npos) {
                    fieldPos += std::strlen("\"description\"");
                    role.description = extractString(roleJson, fieldPos);
                }

                fieldPos = roleJson.find("\"prompt_template\"", 0);
                if (fieldPos != std::string::npos) {
                    fieldPos += std::strlen("\"prompt_template\"");
                    role.promptTemplate = extractString(roleJson, fieldPos);
                }

                fieldPos = roleJson.find("\"visible_tools\"", 0);
                if (fieldPos != std::string::npos) {
                    fieldPos += std::strlen("\"visible_tools\"");
                    role.visibleTools = extractStringArray(roleJson, fieldPos);
                }

                fieldPos = roleJson.find("\"output_format\"", 0);
                if (fieldPos != std::string::npos) {
                    fieldPos += std::strlen("\"output_format\"");
                    role.outputFormat = extractString(roleJson, fieldPos);
                }

                if (!role.name.empty()) {
                    roles_.push_back(std::move(role));
                }

                objStart = std::string::npos;
            }
        }
    }

    return !roles_.empty();
}

const RoleConfig* RoleRegistry::findByName(const std::string& name) const {
    for (const auto& role : roles_) {
        if (role.name == name) {
            return &role;
        }
    }
    return nullptr;
}

} // namespace codepilot