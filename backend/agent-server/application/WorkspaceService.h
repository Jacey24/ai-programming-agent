#pragma once

#include "infrastructure/filesystem/Workspace.h"
#include "infrastructure/storage/repositories/WorkspaceRepository.h"

#include <optional>
#include <string>
#include <vector>

namespace codepilot {

struct ValidateWorkspaceResult {
    bool valid;
    std::string path;
    bool has_git;
    bool has_cmake;
    std::vector<std::string> ignored_dirs;
};

class WorkspaceService {
public:
    explicit WorkspaceService(sqlite3* db);

    std::vector<WorkspaceRecord> listWorkspaces();
    std::optional<WorkspaceRecord> getWorkspaceById(const std::string& workspace_id);

    // 文件操作（内部通过 ToolSystem 获取 Workspace 实例）
    std::vector<FileEntry> listFiles(const std::string& workspace_id,
                                     const std::string& relative_path,
                                     int depth);
    std::string readFileContent(const std::string& workspace_id,
                                const std::string& relative_path,
                                int start_line,
                                int end_line);
    ValidateWorkspaceResult validateWorkspace(const std::string& workspace_id,
                                              const std::string& path_to_validate);

private:
    sqlite3* db_;

    WorkspaceRecord requireWorkspace(const std::string& workspace_id);
};

} // namespace codepilot
