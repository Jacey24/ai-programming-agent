#pragma once

#include "EventTypes.h"
#include <functional>
#include <map>
#include <memory>
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
// ============================================================
class EventBus {
public:
  EventBus();

  // --- 发布事件 ---
  void publish(const EventData &event);

  // --- 订阅事件 ---
  ListenerId subscribe(EventType type, EventListener listener);
  ListenerId subscribeAll(EventListener listener);

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
    EventListener listener;
  };

  ListenerId nextId_;
  std::vector<Subscription> subscriptions_;
  std::vector<EventData> history_;

  void notifyListeners(const EventData &event);
};

} // namespace codepilot