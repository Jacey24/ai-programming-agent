#include "application/ReplayService.h"

#include "infrastructure/storage/repositories/EventRepository.h"
#include "infrastructure/storage/repositories/FileChangeRepository.h"
#include "infrastructure/storage/repositories/ToolCallRepository.h"

#include <algorithm>
#include <stdexcept>

namespace codepilot {

ReplayService::ReplayService(sqlite3* db) : db_(db) {}

ReplayResult ReplayService::buildReplay(const std::string& task_id) {
    const auto task = TaskRepository(db_).findById(task_id);
    if (!task) {
        throw std::runtime_error("task not found");
    }

    std::vector<ReplayTimelineItem> timeline;

    const auto events = EventRepository(db_).findByTaskId(task_id);
    for (const auto& ev : events) {
        timeline.push_back(ReplayTimelineItem{
            ev.type,
            "",
            "",
            ev.content,
            ev.created_at,
        });
    }

    const auto tool_calls = ToolCallRepository(db_).findByTaskId(task_id);
    for (const auto& tc : tool_calls) {
        timeline.push_back(ReplayTimelineItem{
            "tool_call",
            tc.tool_name,
            "",
            tc.arguments,
            tc.created_at,
        });
    }

    const auto file_changes = FileChangeRepository(db_).findByTaskId(task_id);
    for (const auto& fc : file_changes) {
        timeline.push_back(ReplayTimelineItem{
            "file_change",
            "",
            fc.file_path,
            fc.change_type,
            fc.created_at,
        });
    }

    std::sort(timeline.begin(), timeline.end(),
        [](const ReplayTimelineItem& a, const ReplayTimelineItem& b) {
            return a.created_at < b.created_at;
        });

    return ReplayResult{*task, std::move(timeline)};
}

} // namespace codepilot
