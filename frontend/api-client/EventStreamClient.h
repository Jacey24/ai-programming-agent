#pragma once

#include <nlohmann/json.hpp>
#include <atomic>
#include <functional>
#include <string>
#include <thread>

namespace codepilot {

// A single Server-Sent Event received from the stream.
struct SseEvent {
    std::string type;     // value of the "event:" field (defaults to "message")
    std::string rawData;  // raw string from the "data:" field(s)
    nlohmann::json data;  // rawData parsed as JSON (empty object on parse error)
};

using SseEventCallback = std::function<void(const SseEvent&)>;
using SseErrorCallback = std::function<void(const std::string& message)>;

// Asynchronous SSE client that subscribes to a task's event stream.
//
// connect() launches a background thread that streams events from
//   GET /api/v1/tasks/{taskId}/events
// using a child `curl` process.  Call disconnect() to stop the thread.
class EventStreamClient {
public:
    explicit EventStreamClient(std::string baseUrl = "http://127.0.0.1:8080/api/v1");
    ~EventStreamClient();

    // Start streaming events for `taskId`.  `onEvent` is called from the
    // background thread for every complete SSE message dispatched by the
    // server.  `onError` (optional) is called when the stream ends or fails.
    // Any previous connection is stopped before opening the new one.
    void connect(const std::string& taskId,
                 SseEventCallback onEvent,
                 SseErrorCallback onError = nullptr);

    // Stop the background thread and close the stream.
    void disconnect();

    bool isConnected() const;

private:
    std::string baseUrl_;
    std::atomic<bool> running_{false};
    std::thread streamThread_;

    void streamLoop(std::string url,
                    SseEventCallback onEvent,
                    SseErrorCallback onError);

    static SseEvent buildEvent(const std::string& eventType,
                               const std::string& data);
    static std::string urlEncode(const std::string& value);
};

} // namespace codepilot
