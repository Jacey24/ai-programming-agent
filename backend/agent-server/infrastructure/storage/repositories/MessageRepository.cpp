#include "infrastructure/storage/repositories/MessageRepository.h"

#include <stdexcept>

namespace {

std::optional<std::string> optionalText(sqlite3_stmt *stmt, int column) {
  if (sqlite3_column_type(stmt, column) == SQLITE_NULL) {
    return std::nullopt;
  }
  const auto *value =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, column));
  return std::string(value ? value : "");
}

MessageRecord readMessage(sqlite3_stmt *stmt) {
  const auto text = [stmt](int column) {
    const auto *value =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, column));
    return std::string(value ? value : "");
  };
  return {text(0), text(1), optionalText(stmt, 2), text(3), text(4), text(5),
          sqlite3_column_int64(stmt, 6), optionalText(stmt, 7), text(8)};
}

} // namespace

MessageRepository::MessageRepository(sqlite3 *db) : db_(db) {}

std::int64_t
MessageRepository::nextSequenceNo(const std::string &session_id) {
  sqlite3_stmt *stmt = nullptr;
  const char *sql =
      "SELECT COALESCE(MAX(sequence_no), 0) + 1 FROM messages "
      "WHERE session_id = ?;";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(lastError());
  }
  sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);
  if (sqlite3_step(stmt) != SQLITE_ROW) {
    const std::string error = lastError();
    sqlite3_finalize(stmt);
    throw std::runtime_error(error);
  }
  const auto sequence = sqlite3_column_int64(stmt, 0);
  sqlite3_finalize(stmt);
  return sequence;
}

MessageRecord MessageRepository::createMessage(
    const std::string &id, const std::string &session_id,
    const std::optional<std::string> &task_id, const std::string &role,
    const std::string &message_type, const std::string &content,
    const std::optional<std::string> &source_event_id,
    const std::string &created_at) {
  if (session_id.empty() || content.empty()) {
    throw std::invalid_argument("session_id and content are required");
  }
  if (role != "user" && role != "assistant" && role != "system") {
    throw std::invalid_argument("invalid message role");
  }
  if (message_type != "normal" && message_type != "result" &&
      message_type != "error") {
    throw std::invalid_argument("invalid message_type");
  }

  const std::int64_t sequence_no = nextSequenceNo(session_id);
  sqlite3_stmt *stmt = nullptr;
  const char *sql =
      "INSERT INTO messages (id, session_id, task_id, role, message_type, "
      "content, sequence_no, source_event_id, created_at, updated_at) "
      "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(lastError());
  }
  sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, session_id.c_str(), -1, SQLITE_TRANSIENT);
  if (task_id) {
    sqlite3_bind_text(stmt, 3, task_id->c_str(), -1, SQLITE_TRANSIENT);
  } else {
    sqlite3_bind_null(stmt, 3);
  }
  sqlite3_bind_text(stmt, 4, role.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 5, message_type.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 6, content.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt, 7, sequence_no);
  if (source_event_id) {
    sqlite3_bind_text(stmt, 8, source_event_id->c_str(), -1,
                      SQLITE_TRANSIENT);
  } else {
    sqlite3_bind_null(stmt, 8);
  }
  sqlite3_bind_text(stmt, 9, created_at.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 10, created_at.c_str(), -1, SQLITE_TRANSIENT);

  if (sqlite3_step(stmt) != SQLITE_DONE) {
    const std::string error = lastError();
    sqlite3_finalize(stmt);
    throw std::runtime_error(error);
  }
  sqlite3_finalize(stmt);
  return {id,         session_id, task_id, role, message_type, content,
          sequence_no, source_event_id, created_at};
}

std::optional<MessageRecord>
MessageRepository::findOne(const char *sql, const std::string &value) {
  sqlite3_stmt *stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(lastError());
  }
  sqlite3_bind_text(stmt, 1, value.c_str(), -1, SQLITE_TRANSIENT);
  const int result = sqlite3_step(stmt);
  if (result == SQLITE_DONE) {
    sqlite3_finalize(stmt);
    return std::nullopt;
  }
  if (result != SQLITE_ROW) {
    const std::string error = lastError();
    sqlite3_finalize(stmt);
    throw std::runtime_error(error);
  }
  auto message = readMessage(stmt);
  sqlite3_finalize(stmt);
  return message;
}

std::vector<MessageRecord>
MessageRepository::findMany(const char *sql, const std::string &value) {
  sqlite3_stmt *stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(lastError());
  }
  sqlite3_bind_text(stmt, 1, value.c_str(), -1, SQLITE_TRANSIENT);
  std::vector<MessageRecord> messages;
  int result = SQLITE_ROW;
  while ((result = sqlite3_step(stmt)) == SQLITE_ROW) {
    messages.push_back(readMessage(stmt));
  }
  if (result != SQLITE_DONE) {
    const std::string error = lastError();
    sqlite3_finalize(stmt);
    throw std::runtime_error(error);
  }
  sqlite3_finalize(stmt);
  return messages;
}

std::optional<MessageRecord>
MessageRepository::findById(const std::string &id) {
  return findOne(
      "SELECT id, session_id, task_id, role, message_type, content, "
      "sequence_no, source_event_id, created_at FROM messages WHERE id = ?;",
      id);
}

std::optional<MessageRecord> MessageRepository::findBySourceEventId(
    const std::string &source_event_id) {
  return findOne(
      "SELECT id, session_id, task_id, role, message_type, content, "
      "sequence_no, source_event_id, created_at FROM messages "
      "WHERE source_event_id = ?;",
      source_event_id);
}

std::vector<MessageRecord>
MessageRepository::findBySessionId(const std::string &session_id) {
  return findMany(
      "SELECT id, session_id, task_id, role, message_type, content, "
      "sequence_no, source_event_id, created_at FROM messages "
      "WHERE session_id = ? ORDER BY sequence_no ASC;",
      session_id);
}

std::vector<MessageRecord>
MessageRepository::findByTaskId(const std::string &task_id) {
  return findMany(
      "SELECT id, session_id, task_id, role, message_type, content, "
      "sequence_no, source_event_id, created_at FROM messages "
      "WHERE task_id = ? ORDER BY sequence_no ASC;",
      task_id);
}

std::optional<MessageRecord> MessageRepository::findTaskMessage(
    const std::string &task_id, const std::string &role,
    const std::string &message_type) {
  sqlite3_stmt *stmt = nullptr;
  const char *sql =
      "SELECT id, session_id, task_id, role, message_type, content, "
      "sequence_no, source_event_id, created_at FROM messages "
      "WHERE task_id = ? AND role = ? AND message_type = ? "
      "ORDER BY sequence_no ASC LIMIT 1;";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(lastError());
  }
  sqlite3_bind_text(stmt, 1, task_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, role.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, message_type.c_str(), -1, SQLITE_TRANSIENT);
  const int result = sqlite3_step(stmt);
  if (result == SQLITE_DONE) {
    sqlite3_finalize(stmt);
    return std::nullopt;
  }
  if (result != SQLITE_ROW) {
    const std::string error = lastError();
    sqlite3_finalize(stmt);
    throw std::runtime_error(error);
  }
  auto message = readMessage(stmt);
  sqlite3_finalize(stmt);
  return message;
}

std::optional<MessageRecord>
MessageRepository::findFinalAssistantMessage(const std::string &task_id) {
  sqlite3_stmt *stmt = nullptr;
  const char *sql =
      "SELECT id, session_id, task_id, role, message_type, content, "
      "sequence_no, source_event_id, created_at FROM messages "
      "WHERE task_id = ? AND role = 'assistant' "
      "AND message_type IN ('result', 'error') "
      "ORDER BY sequence_no ASC LIMIT 1;";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(lastError());
  }
  sqlite3_bind_text(stmt, 1, task_id.c_str(), -1, SQLITE_TRANSIENT);
  const int result = sqlite3_step(stmt);
  if (result == SQLITE_DONE) {
    sqlite3_finalize(stmt);
    return std::nullopt;
  }
  if (result != SQLITE_ROW) {
    const std::string error = lastError();
    sqlite3_finalize(stmt);
    throw std::runtime_error(error);
  }
  auto message = readMessage(stmt);
  sqlite3_finalize(stmt);
  return message;
}

std::string MessageRepository::lastError() const {
  return db_ ? sqlite3_errmsg(db_) : "sqlite database is not open";
}
