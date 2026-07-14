#pragma once

#include <sqlite3.h>

#include <string>
#include <vector>

namespace codepilot {

// ============================================================
// MigrationRunner — 数据库版本化迁移引擎
//
// 设计原则：
//   - 内置迁移 SQL（不依赖外部 .sql 文件，保证可移植性）
//   - 幂等执行：已应用的版本跳过
//   - 事务性：所有未应用迁移在单个事务中执行
//   - 只增不减：不支持回滚，靠正向 ALTER 演进 schema
//
// 使用方式：
//   MigrationRunner runner(db);
//   runner.migrate();
// ============================================================
class MigrationRunner {
public:
  struct Migration {
    int version;
    std::string name;
    std::string sql;
  };

  explicit MigrationRunner(sqlite3 *db);

  // 执行所有未应用的迁移
  // 返回 true 表示成功（包括无新迁移的情况）
  bool migrate();

  // 查询当前数据库版本（已应用的最高版本号）
  // 返回 0 表示 schema_migrations 表为空或不存在
  int currentVersion();

private:
  sqlite3 *db_;

  void ensureSchemaMigrationsTable();
  std::vector<Migration> getPendingMigrations();
  bool applyMigration(const Migration &m);

  // 所有内置迁移定义（按版本号升序）
  static std::vector<Migration> builtinMigrations();

  std::string lastError() const;
};

} // namespace codepilot