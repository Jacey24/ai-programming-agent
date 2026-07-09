#pragma once

#include "EventTypes.h"
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

using json = nlohmann::json;

namespace codepilot {

// ============================================================
// 事件数据体
// 对齐接口文档.md 第3.5节
// ============================================================
struct EventData {
  std::string id;
  std::string taskId;
  EventType type;
  std::string content;
  json metadata;         // 支持任意 JSON 结构
  std::string createdAt; // ISO 8601 格式

  std::string typeToString() const;
  std::string serialize() const;

  static EventData Create(const std::string &taskId, EventType type,
                          const std::string &content,
                          const json &metadata = json::object());
};

// ============================================================
// 监听器回调
// ============================================================
using EventListener = std::function<void(const EventData &)>;
using ListenerId = size_t;

// ============================================================
// EventBus - 事件发布-订阅总线
//
// Sprint 2 新增能力：
//   1. subscribeByTaskId(taskId, listener) — 按 taskId 订阅
//      郑嘉娴的 SSE 端点使用此接口注册每个 task 的推送回调
//   2. onEvent(listener) — 全局事件钩子
//      钟经添的 Repository 使用此接口持久化事件到 SQLite
//   3. 所有方法线程安全（mutex 保护）
// ============================================================
class EventBus {
public:
  EventBus();

  // --- 发布事件 ---
  // 线程安全
  void publish(const EventData &event);

  // --- 按类型订阅 ---
  ListenerId subscribe(EventType type, EventListener listener);

  // --- 订阅所有事件 ---
  ListenerId subscribeAll(EventListener listener);

  // --- ★ 新增：按 taskId 订阅 ---
  // 只接收指定 taskId 的事件
  // 用于郑嘉娴的 SSE 端点给每个 task 注册推送
  ListenerId subscribeByTaskId(const std::string &taskId,
                               EventListener listener);

  // --- ★ 新增：全局事件钩子（用于持久化等） ---
  // 接收所有事件，过滤条件由回调内部自行判断
  // 钟经添的 Repository 用此接口持久化事件
  ListenerId onEvent(EventListener listener);

  // --- 取消订阅 ---
  void unsubscribe(ListenerId id);

  // --- 获取事件历史 ---
  std::vector<EventData> getHistory(const std::string &taskId = "") const;
  std::vector<EventData> getHistoryByType(EventType type,
                                          const std::string &taskId = "") const;

  // --- 清空历史 ---
  void clearHistory(const std::string &taskId = "");

private:
  struct Subscription {
    ListenerId id;
    EventType type;
    bool allEvents{false};
    bool allTasks{false}; // 匹配所有 taskId
    std::string taskId;   // 如果非空，只匹配此 taskId 的事件
    bool global{false};   // 全局钩子（onEvent）
    EventListener listener;
  };

  ListenerId nextId_;
  std::vector<Subscription> subscriptions_;
  std::vector<EventData> history_;
  mutable std::mutex mutex_; // 线程安全互斥锁

  void notifyListeners(const EventData &event);
};

} // namespace codepilot