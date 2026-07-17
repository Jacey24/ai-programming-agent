#pragma once

#include <sqlite3.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct MessageRecord {
  std::string id;
  std::string session_id;
  std::optional<std::string> task_id;
  std::string role;
  std::string message_type;
  std::string content;
  std::int64_t sequence_no = 0;
  std::optional<std::string> source_event_id;
  std::string created_at;
};

class MessageRepository {
public:
  explicit MessageRepository(sqlite3 *db);

  MessageRecord createMessage(
      const std::string &id, const std::string &session_id,
      const std::optional<std::string> &task_id, const std::string &role,
      const std::string &message_type, const std::string &content,
      const std::optional<std::string> &source_event_id,
      const std::string &created_at);
  std::optional<MessageRecord> findById(const std::string &id);
  std::optional<MessageRecord>
  findBySourceEventId(const std::string &source_event_id);
  std::vector<MessageRecord> findBySessionId(const std::string &session_id);
  std::vector<MessageRecord> findByTaskId(const std::string &task_id);
  std::optional<MessageRecord> findTaskMessage(const std::string &task_id,
                                               const std::string &role,
                                               const std::string &message_type);
  std::optional<MessageRecord>
  findFinalAssistantMessage(const std::string &task_id);

private:
  sqlite3 *db_;

  std::int64_t nextSequenceNo(const std::string &session_id);
  std::optional<MessageRecord> findOne(const char *sql,
                                       const std::string &value);
  std::vector<MessageRecord> findMany(const char *sql,
                                      const std::string &value);
  std::string lastError() const;
};
