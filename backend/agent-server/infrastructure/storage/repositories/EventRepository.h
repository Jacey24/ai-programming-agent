#pragma once

#include <sqlite3.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct EventRecord {
    std::string id;
    std::string task_id;
    std::string type;
    std::string content;
    std::string metadata;
    std::string created_at;
    std::int64_t sequence_no{0};
};

class EventRepository {
public:
    explicit EventRepository(sqlite3* db);

    void initTable();
    EventRecord create(
        const std::string& id,
        const std::string& task_id,
        const std::string& type,
        const std::string& content,
        const std::string& metadata);
    std::optional<EventRecord> findById(const std::string& id);
    std::vector<EventRecord> findByTaskId(const std::string& task_id);
    std::vector<EventRecord> findByTaskIdAfterSequence(
        const std::string& task_id, std::int64_t after_sequence);
    std::vector<EventRecord> findByTaskIdAndType(const std::string& task_id, const std::string& type);

private:
    sqlite3* db_;

    std::string lastError() const;
};
