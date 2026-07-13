#pragma once

#include <sqlite3.h>

#include <optional>
#include <string>
#include <vector>

struct GlobalRecord {
  std::string id;
  std::string name;
  std::string description;
  std::string created_at;
  std::string updated_at;
};

struct GlobalContextRecord {
  int64_t id = 0;
  std::string global_id;
  std::string source_task_id;
  std::string type; // 'summary' | 'plan' | 'output'
  std::string content;
  std::string created_at;
};

class GlobalRepository {
public:
  explicit GlobalRepository(sqlite3 *db);

  void initTable();
  void initContextTable();

  // ── Global CRUD ──
  GlobalRecord createGlobal(const std::string &id, const std::string &name,
                            const std::string &description,
                            const std::string &created_at,
                            const std::string &updated_at);
  std::optional<GlobalRecord> findById(const std::string &global_id);
  std::vector<GlobalRecord> listAll();
  bool deleteById(const std::string &global_id);
  int count();

  // ── Global Context ──
  void saveContext(const std::string &global_id,
                   const std::string &source_task_id, const std::string &type,
                   const std::string &content, const std::string &created_at);
  std::vector<GlobalContextRecord>
  getContextByGlobalId(const std::string &global_id);

  // ★ 确保默认 Global 存在
  std::string ensureDefaultGlobal();

private:
  sqlite3 *db_;

  std::string lastError() const;
};