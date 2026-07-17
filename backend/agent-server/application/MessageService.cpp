#include "application/MessageService.h"

#include <stdexcept>

namespace codepilot {

MessageService::MessageService(sqlite3 *db) : db_(db) {}

MessageRecord MessageService::createMessage(
    const std::string &id, const std::string &session_id,
    const std::optional<std::string> &task_id, const std::string &role,
    const std::string &message_type, const std::string &content,
    const std::optional<std::string> &source_event_id,
    const std::string &created_at) {
  char *error = nullptr;
  if (sqlite3_exec(db_, "BEGIN IMMEDIATE;", nullptr, nullptr, &error) !=
      SQLITE_OK) {
    const std::string message = error ? error : sqlite3_errmsg(db_);
    sqlite3_free(error);
    throw std::runtime_error(message);
  }
  try {
    auto message = MessageRepository(db_).createMessage(
        id, session_id, task_id, role, message_type, content, source_event_id,
        created_at);
    if (sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, &error) != SQLITE_OK) {
      const std::string commitError = error ? error : sqlite3_errmsg(db_);
      sqlite3_free(error);
      throw std::runtime_error(commitError);
    }
    return message;
  } catch (...) {
    sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
    throw;
  }
}

std::vector<MessageRecord>
MessageService::findBySessionId(const std::string &session_id) {
  return MessageRepository(db_).findBySessionId(session_id);
}

std::vector<MessageRecord>
MessageService::findByTaskId(const std::string &task_id) {
  return MessageRepository(db_).findByTaskId(task_id);
}

std::optional<MessageRecord> MessageService::findById(const std::string &id) {
  return MessageRepository(db_).findById(id);
}

std::optional<MessageRecord> MessageService::findBySourceEventId(
    const std::string &source_event_id) {
  return MessageRepository(db_).findBySourceEventId(source_event_id);
}

} // namespace codepilot
