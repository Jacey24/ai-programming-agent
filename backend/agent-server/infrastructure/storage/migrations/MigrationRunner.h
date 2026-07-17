#pragma once

#include <sqlite3.h>

#include <map>
#include <string>
#include <vector>

namespace codepilot {

// ============================================================
// MigrationRunner — 数据库版本化迁移引擎
//
// 设计原则：
//   - 内置迁移 SQL（不依赖外部 .sql 文件，保证可移植性）
//   - 幂等执行：版本记录和真实结构同时核验，安全步骤可重复执行
//   - 事务性：每个迁移的结构变更和版本记录在同一事务中执行
//   - 只增不减：不支持回滚，靠正向 ALTER 演进 schema
//
// 使用方式：
//   MigrationRunner runner(db);
//   runner.migrate();
// ============================================================
class MigrationRunner {
public:
  struct ColumnRequirement {
    std::string table;
    std::string column;
    std::string type;
    bool notNull;
    std::string defaultValue;
  };

  struct MigrationStep {
    std::string sql;
    bool hasColumnRequirement{false};
    ColumnRequirement columnRequirement;
  };

  struct Migration {
    int version;
    std::string name;
    std::vector<MigrationStep> steps;
  };

  explicit MigrationRunner(sqlite3 *db);

  // 执行所有未应用的迁移
  // 返回 true 表示成功；失败时抛出异常且当前迁移会回滚
  bool migrate();

  // 查询当前数据库版本（已应用的最高版本号）
  // 返回 0 表示 schema_migrations 表为空或不存在
  int currentVersion();

private:
  sqlite3 *db_;

  void ensureSchemaMigrationsTable();
  std::map<int, std::string> appliedMigrations();
  void applyMigration(const Migration &migration);
  void applyStep(const Migration &migration, const MigrationStep &step,
                 size_t stepIndex);
  void executeRequired(const std::string &sql, const std::string &context);
  bool columnMatches(const ColumnRequirement &requirement,
                     bool &columnExists);

  // 所有内置迁移定义（按版本号升序）
  static std::vector<Migration> builtinMigrations();

  std::string lastError() const;
};

} // namespace codepilot
