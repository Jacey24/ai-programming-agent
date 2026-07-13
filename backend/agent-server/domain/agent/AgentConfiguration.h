#pragma once

#include "ExpertConfig.h"
#include <mutex>
#include <string>
#include <vector>

namespace codepilot {

// ============================================================
// AgentConfiguration — Agent 配置的全生命周期管理器
//
// 职责：
//   1. 从 JSON 文件加载 Expert 配置
//   2. 支持运行时 reconfigure（每 call 前重新读取）
//   3. 导出/导入 JSON（供前端配置 UI 使用）
//   4. 持久化到磁盘
//   5. 按 Expert 查询 LLM 配置
//
// 线程安全：所有 public 方法加 mutex
// ============================================================
class AgentConfiguration {
public:
  // ── 单例（与 ToolSystem/DataAccessFacade 保持一致）──
  static AgentConfiguration &getInstance();
  AgentConfiguration(const AgentConfiguration &) = delete;
  AgentConfiguration &operator=(const AgentConfiguration &) = delete;

  // ── 初始化 ──
  bool init(const std::string &configPath);

  // ── 运行时重配置（每 call 前调用）──
  bool reconfigure();

  // ── JSON 导入导出（前端交互接口）──
  std::string exportJson() const;              // 导出当前内存中的完整配置
  bool importJson(const std::string &jsonStr); // 从 JSON 导入配置（更新内存）

  // ── 持久化 ──
  bool saveToFile() const;                              // 保存到当前 configPath
  bool saveToFile(const std::string &targetPath) const; // 保存到指定路径

  // ── 查询 ──
  const ExpertConfig *getExpert(const std::string &name) const;
  const ExpertConfig *getEntryExpert() const;
  std::vector<std::string> listExpertNames() const;
  std::vector<ExpertConfig> allExperts() const;
  bool isReady() const;

  // ── Expert CRUD（单 Expert 粒度的增删改）──
  // 返回 false 表示名称冲突或未找到
  bool addExpert(const ExpertConfig &expert); // 创建新 Expert
  bool updateExpert(const std::string &name,
                    const ExpertConfig &config); // 全量更新
  bool patchExpert(const std::string &name,
                   const std::string &jsonPatch); // 局部更新（JSON merge）
  bool removeExpert(const std::string &name);     // 删除 Expert

  // ── Expert 子字段更新（精细控制）──
  bool setExpertTools(const std::string &name,
                      const std::vector<std::string> &tools);
  bool setExpertRoutes(const std::string &name,
                       const std::vector<RouteRule> &routes);
  bool addExpertRoute(const std::string &name, const RouteRule &rule);
  bool removeExpertRoute(const std::string &name, int index);
  bool setExpertLlm(const std::string &name, const std::string &provider,
                    const std::string &model, int timeout, double temperature);

  // ── 全局 LLM 默认值 ──
  struct GlobalLlmDefaults {
    std::string provider;
    std::string model;
    int timeout = 0;
    double temperature = -1.0;
  };
  GlobalLlmDefaults getGlobalLlmDefaults() const;
  bool setGlobalLlmDefaults(const GlobalLlmDefaults &defaults);

  // ── 配置验证（不修改内存状态）──
  static bool validate(const std::string &jsonStr, std::string &errorOut);

  // ── 配置路径 ──
  std::string configPath() const;

  // 原始 JSON 字符串（供前端直接展示）
  std::string rawJson() const;

private:
  AgentConfiguration() = default;

  // 重建 rawJson_ 从当前 experts_
  void rebuildRawJson();

  std::string configPath_;
  std::string rawJson_;               // 原始 JSON 字符串
  std::vector<ExpertConfig> experts_; // 反序列化后的配置
  mutable std::mutex mutex_;
};

} // namespace codepilot