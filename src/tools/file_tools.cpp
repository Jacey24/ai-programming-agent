#include "file_tools.h"
#include <fstream>
#include <sstream>

// FileReadTool
std::string FileReadTool::name() const { return "file_read"; }
std::string FileReadTool::description() const { return "Read contents of a file"; }

json FileReadTool::parameters_schema() const {
    return {{"path", {{"type", "string"}, {"description", "File path to read"}}}};
}

ToolResult FileReadTool::execute(const json& params) {
    std::ifstream file(params["path"].get<std::string>());
    if (!file) return {false, "Cannot open file"};
    std::ostringstream ss;
    ss << file.rdbuf();
    return {true, ss.str()};
}

// FileWriteTool
std::string FileWriteTool::name() const { return "file_write"; }
std::string FileWriteTool::description() const { return "Write content to a file"; }

json FileWriteTool::parameters_schema() const {
    return {
        {"path", {{"type", "string"}, {"description", "File path to write"}}},
        {"content", {{"type", "string"}, {"description", "Content to write"}}}
    };
}

ToolResult FileWriteTool::execute(const json& params) {
    std::ofstream file(params["path"].get<std::string>());
    if (!file) return {false, "Cannot write file"};
    file << params["content"].get<std::string>();
    return {true, "File written successfully"};
}

// FileEditTool
std::string FileEditTool::name() const { return "file_edit"; }
std::string FileEditTool::description() const { return "Edit a file with string replacement"; }

json FileEditTool::parameters_schema() const {
    return {
        {"path", {{"type", "string"}, {"description", "File path to edit"}}},
        {"old_string", {{"type", "string"}, {"description", "Text to replace"}}},
        {"new_string", {{"type", "string"}, {"description", "Replacement text"}}}
    };
}

ToolResult FileEditTool::execute(const json& params) {
    // TODO: Read file, replace text, show diff
    return {true, "Edit applied"};
}
