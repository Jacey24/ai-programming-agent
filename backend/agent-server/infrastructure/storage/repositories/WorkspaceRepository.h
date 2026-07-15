#pragma once

#include <sqlite3.h>

#include <optional>
#include <string>
#include <vector>

struct WorkspaceRecord {
  std::string id;
  std::string name;
  std::string path;
  std::string description;
  std::string last_opened_at;
  std::string permissions_config;
  std::string created_at;
};

class WorkspaceRepository {
public:
  explicit WorkspaceRepository(sqlite3 *db);

  void initTable();
  WorkspaceRecord create(const std::string &id, const std::string &name,
                         const std::string &path, const std::string &created_at,
                         const std::string &permissions_config = "{}");
  std::optional<WorkspaceRecord> findById(const std::string &workspace_id);
  std::vector<WorkspaceRecord> listAll();
  bool deleteById(const std::string &workspace_id);

private:
  sqlite3 *db_;

  std::string lastError() const;
};