#include "EventBus.h"
#include <algorithm>
#include <chrono>
#include <ctime>
#include <sstream>

namespace codepilot {

EventBus::EventBus() : nextId_(1) {}

// ============================================================
// 工具函数：生成 ISO 8601 时间字符串
// 对齐接口文档.md 第2.5节
// ============================================================
static std::string currentTimestamp() {
  auto now = std::chrono::system_clock::now();
  std::time_t t = std::chrono::system_clock::to_time_t(now);
  std::tm tm;
  // Windows 安全版本
#if defined(_WIN32)
  gmtime_s(&tm, &t);
#else
  gmtime_r(&t, &tm);
#endif
  char buf[64];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
  return std::string(buf);
}

// ============================================================
// EventData 实现
// ============================================================

std::string EventData::typeToString() const {
  switch (type) {
  case EventType::TaskCreated:
    return "task_created";
  case EventType::TaskPlanning:
    return "task_planning";
  case EventType::AgentMessage:
    return "agent_message";
  case EventType::ToolStarted:
    return "tool_started";
  case EventType::ToolOutput:
    return "tool_output";
  case EventType::ToolFinished:
    return "tool_finished";
  case EventType::PermissionRequired:
    return "permission_required";
  case EventType::PermissionResolved:
    return "permission_resolved";
  case EventType::FileChanged:
    return "file_changed";
  case EventType::TaskCompleted:
    return "task_completed";
  case EventType::TaskFailed:
    return "task_failed";
  case EventType::TaskCancelled:
    return "task_cancelled";
  default:
    return "unknown";
  }
}

std::string EventData::serialize() const {
  json j;
  j["id"] = id;
  j["task_id"] = taskId;
  j["type"] = typeToString();
  j["content"] = content;

  // metadata 本身就是 json 类型，直接赋值
  j["metadata"] = metadata;

  j["created_at"] = createdAt;
  return j.dump(2);
}

EventData EventData::Create(const std::string &taskId, EventType type,
                            const std::string &content, const json &metadata) {
  static size_t counter = 0;
  EventData data;
  data.id = "event_" + std::to_string(++counter);
  data.taskId = taskId;
  data.type = type;
  data.content = content;
  data.metadata = metadata;
  data.createdAt = currentTimestamp();
  return data;
}

// ============================================================
// EventBus 实现（线程安全）
// ============================================================

void EventBus::publish(const EventData &event) {
  std::lock_guard<std::mutex> lock(mutex_);
  history_.push_back(event);
  notifyListeners(event);
}

ListenerId EventBus::subscribe(EventType type, EventListener listener) {
  std::lock_guard<std::mutex> lock(mutex_);
  ListenerId id = nextId_++;
  subscriptions_.push_back(
      {id, type, false, false, "", false, std::move(listener)});
  return id;
}

ListenerId EventBus::subscribeAll(EventListener listener) {
  std::lock_guard<std::mutex> lock(mutex_);
  ListenerId id = nextId_++;
  subscriptions_.push_back({id, EventType::TaskCreated, true, false, "", false,
                            std::move(listener)});
  return id;
}

// ============================================================
// ★ 新增：subscribeByTaskId — 按 taskId 订阅
// ============================================================
ListenerId EventBus::subscribeByTaskId(const std::string &taskId,
                                       EventListener listener) {
  std::lock_guard<std::mutex> lock(mutex_);
  ListenerId id = nextId_++;
  subscriptions_.push_back({id, EventType::TaskCreated, false, false, taskId,
                            false, std::move(listener)});
  return id;
}

// ============================================================
// ★ 新增：onEvent — 全局事件钩子
// ============================================================
ListenerId EventBus::onEvent(EventListener listener) {
  std::lock_guard<std::mutex> lock(mutex_);
  ListenerId id = nextId_++;
  subscriptions_.push_back({id, EventType::TaskCreated, false, false, "", true,
                            std::move(listener)});
  return id;
}

void EventBus::unsubscribe(ListenerId id) {
  std::lock_guard<std::mutex> lock(mutex_);
  subscriptions_.erase(
      std::remove_if(subscriptions_.begin(), subscriptions_.end(),
                     [id](const Subscription &s) { return s.id == id; }),
      subscriptions_.end());
}

std::vector<EventData> EventBus::getHistory(const std::string &taskId) const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (taskId.empty())
    return history_;
  std::vector<EventData> result;
  for (const auto &event : history_) {
    if (event.taskId == taskId)
      result.push_back(event);
  }
  return result;
}

std::vector<EventData>
EventBus::getHistoryByType(EventType type, const std::string &taskId) const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<EventData> result;
  for (const auto &event : history_) {
    if (event.type == type) {
      if (taskId.empty() || event.taskId == taskId) {
        result.push_back(event);
      }
    }
  }
  return result;
}

void EventBus::clearHistory(const std::string &taskId) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (taskId.empty()) {
    history_.clear();
  } else {
    history_.erase(std::remove_if(history_.begin(), history_.end(),
                                  [&taskId](const EventData &e) {
                                    return e.taskId == taskId;
                                  }),
                   history_.end());
  }
}

void EventBus::notifyListeners(const EventData &event) {
  for (const auto &sub : subscriptions_) {
    if (!sub.listener)
      continue;

    // 全局钩子（onEvent）：接收所有事件，不过滤
    if (sub.global) {
      sub.listener(event);
      continue;
    }

    // 按 taskId 订阅：只匹配指定 taskId
    if (!sub.taskId.empty()) {
      if (sub.taskId != event.taskId)
        continue;
      // 如果还指定了类型，按类型过滤
      if (!sub.allEvents && sub.type != event.type)
        continue;
      sub.listener(event);
      continue;
    }

    // 普通订阅（按类型或全部）
    if (!sub.allEvents && sub.type != event.type)
      continue;
    sub.listener(event);
  }
}

} // namespace codepilot