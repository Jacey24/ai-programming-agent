#pragma once

#include "infrastructure/storage/repositories/MessageRepository.h"

namespace codepilot {

class MessageService {
public:
  explicit MessageService(sqlite3 *db);

  MessageRecord createMessage(
      const std::string &id, const std::string &session_id,
      const std::optional<std::string> &task_id, const std::string &role,
      const std::string &message_type, const std::string &content,
      const std::optional<std::string> &source_event_id,
      const std::string &created_at);
  std::vector<MessageRecord> findBySessionId(const std::string &session_id);
  std::vector<MessageRecord> findByTaskId(const std::string &task_id);
  std::optional<MessageRecord> findById(const std::string &id);
  std::optional<MessageRecord>
  findBySourceEventId(const std::string &source_event_id);

private:
  sqlite3 *db_;
};

} // namespace codepilot
