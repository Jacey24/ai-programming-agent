#include "application/WorkspaceService.h"
#include "application/ToolSystem.h"

#include <filesystem>
#include <stdexcept>

namespace codepilot {

WorkspaceService::WorkspaceService(sqlite3* db) : db_(db) {}

std::vector<WorkspaceRecord> WorkspaceService::listWorkspaces() {
    return WorkspaceRepository(db_).listAll();
}

std::optional<WorkspaceRecord> WorkspaceService::getWorkspaceById(const std::string& workspace_id) {
    return WorkspaceRepository(db_).findById(workspace_id);
}

WorkspaceRecord WorkspaceService::requireWorkspace(const std::string& workspace_id) {
    auto record = WorkspaceRepository(db_).findById(workspace_id);
    if (!record) {
        throw std::runtime_error("workspace not found");
    }
    return *record;
}

std::vector<FileEntry> WorkspaceService::listFiles(
        const std::string& workspace_id,
        const std::string& relative_path,
        int depth) {
    requireWorkspace(workspace_id);  // 确保 workspace 存在

    auto& tool_system = ToolSystem::getInstance();
    if (!tool_system.isInitialized()) {
        throw std::runtime_error("tool system not initialized");
    }

    return tool_system.workspace().listFiles(relative_path, depth);
}

std::string WorkspaceService::readFileContent(
        const std::string& workspace_id,
        const std::string& relative_path,
        int start_line,
        int end_line) {
    requireWorkspace(workspace_id);

    auto& tool_system = ToolSystem::getInstance();
    if (!tool_system.isInitialized()) {
        throw std::runtime_error("tool system not initialized");
    }

    auto& ws = tool_system.workspace();

    // 安全检查
    if (!ws.isPathSafe(relative_path)) {
        throw std::runtime_error("path is outside workspace");
    }

    // 二进制文件检查
    if (ws.isBinaryFile(relative_path)) {
        throw std::runtime_error("binary file not supported");
    }

    // 文件大小检查（最大 1MB）
    const int64_t file_size = ws.getFileSize(relative_path);
    if (file_size > 1024 * 1024) {
        throw std::runtime_error("file too large");
    }

    return ws.readFile(relative_path, start_line, end_line);
}

ValidateWorkspaceResult WorkspaceService::validateWorkspace(
        const std::string& workspace_id,
        const std::string& path_to_validate) {
    const auto record = requireWorkspace(workspace_id);

    auto& tool_system = ToolSystem::getInstance();
    if (!tool_system.isInitialized()) {
        throw std::runtime_error("tool system not initialized");
    }

    auto& ws = tool_system.workspace();

    ValidateWorkspaceResult result;
    result.valid = ws.isPathWithinWorkspace(
        std::filesystem::path(record.path) / path_to_validate);
    result.path = path_to_validate;

    // 检查是否有 .git 目录
    const auto git_path = std::filesystem::path(record.path) / ".git";
    result.has_git = std::filesystem::exists(git_path);

    // 检查是否有 CMakeLists.txt
    const auto cmake_path = std::filesystem::path(record.path) / "CMakeLists.txt";
    result.has_cmake = std::filesystem::exists(cmake_path);

    // 默认忽略目录
    result.ignored_dirs = Workspace::defaultIgnoredDirs();

    return result;
}

} // namespace codepilot
