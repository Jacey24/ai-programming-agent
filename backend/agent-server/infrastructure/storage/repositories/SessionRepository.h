#pragma once

#include <sqlite3.h>

#include <optional>
#include <string>
#include <vector>

struct SessionRecord {
  std::string id;
  std::string title;
  std::string alias;
  std::string workspace_id;
  std::string summary;
  std::string summary_updated_at;
  std::string last_active_at;
  std::string created_at;
  std::string updated_at;
};

class SessionRepository {
public:
  explicit SessionRepository(sqlite3 *db);

  void initTable();
  SessionRecord createSession(const std::string &id, const std::string &title,
                              const std::string &created_at,
                              const std::string &updated_at);
  std::optional<SessionRecord> findById(const std::string &session_id);
  std::vector<SessionRecord> listAll();
  std::vector<SessionRecord>
  findByWorkspaceId(const std::string &workspace_id,
                    const std::string &orderBy = "created_at DESC",
                    int limit = -1);
  bool deleteById(const std::string &session_id);

private:
  sqlite3 *db_;

  std::string lastError() const;
};