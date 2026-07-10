#include "api-client/EventStreamClient.h"

#include <cstdio>
#include <sstream>
#include <string>

using json = nlohmann::json;

namespace codepilot {

namespace {

// Cross-platform shell quoting for curl arguments.
std::string shellQuote(const std::string& value) {
#ifdef _WIN32
    std::string quoted = "\"";
    for (char ch : value) {
        if (ch == '"') {
            quoted += "\\\"";
        } else {
            quoted += ch;
        }
    }
    quoted += "\"";
    return quoted;
#else
    std::string quoted = "'";
    for (char ch : value) {
        if (ch == '\'') {
            quoted += "'\\''";
        } else {
            quoted += ch;
        }
    }
    quoted += "'";
    return quoted;
#endif
}

// Strip trailing CR and LF from `line`.
void stripLineEnding(std::string& line) {
    while (!line.empty() &&
           (line.back() == '\n' || line.back() == '\r')) {
        line.pop_back();
    }
}

} // namespace

// ---------------------------------------------------------------------------
// EventStreamClient
// ---------------------------------------------------------------------------

EventStreamClient::EventStreamClient(std::string baseUrl)
    : baseUrl_(std::move(baseUrl)) {
    while (!baseUrl_.empty() && baseUrl_.back() == '/') {
        baseUrl_.pop_back();
    }
}

EventStreamClient::~EventStreamClient() {
    disconnect();
}

void EventStreamClient::connect(const std::string& taskId,
                                 SseEventCallback onEvent,
                                 SseErrorCallback onError) {
    disconnect();

    const std::string url =
        baseUrl_ + "/tasks/" + urlEncode(taskId) + "/events";

    running_.store(true);
    streamThread_ =
        std::thread(&EventStreamClient::streamLoop, this, url, onEvent, onError);
}

void EventStreamClient::disconnect() {
    running_.store(false);
    if (streamThread_.joinable()) {
        streamThread_.join();
    }
}

bool EventStreamClient::isConnected() const {
    return running_.load();
}

// ---------------------------------------------------------------------------
// Background stream loop
// ---------------------------------------------------------------------------

void EventStreamClient::streamLoop(std::string url,
                                    SseEventCallback onEvent,
                                    SseErrorCallback onError) {
    // curl flags:
    //   -sS            : silent but show errors
    //   --no-buffer    : disable output buffering for real-time delivery
    //   --max-time 0   : no overall timeout (stream runs until disconnect)
    //   -N             : alias for --no-buffer (belt-and-suspenders)
    std::ostringstream cmd;
    cmd << "curl -sS -N --no-buffer --max-time 0"
        << " -H " << shellQuote("Accept: text/event-stream")
        << " -H " << shellQuote("Cache-Control: no-cache")
        << " " << shellQuote(url);

#ifdef _WIN32
    FILE* pipe = _popen(cmd.str().c_str(), "r");
#else
    FILE* pipe = popen(cmd.str().c_str(), "r");
#endif

    if (!pipe) {
        if (onError) {
            onError("failed to open SSE stream (popen returned null)");
        }
        running_.store(false);
        return;
    }

    // SSE parsing state for the current message block.
    std::string currentEventType;  // value of the last "event:" field
    std::string currentData;       // accumulated "data:" lines

    char buffer[4096];
    while (running_.load() && fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        std::string line(buffer);
        stripLineEnding(line);

        if (line.empty()) {
            // An empty line dispatches the buffered message.
            if (!currentData.empty()) {
                const SseEvent evt =
                    buildEvent(currentEventType, currentData);
                onEvent(evt);
            }
            currentEventType.clear();
            currentData.clear();
            continue;
        }

        // "event: <type>"
        if (line.compare(0, 6, "event:") == 0) {
            currentEventType = line.substr(6);
            if (!currentEventType.empty() && currentEventType.front() == ' ') {
                currentEventType.erase(currentEventType.begin());
            }
            continue;
        }

        // "data: <payload>" — multiple data lines are joined with '\n'.
        if (line.compare(0, 5, "data:") == 0) {
            std::string dataLine = line.substr(5);
            if (!dataLine.empty() && dataLine.front() == ' ') {
                dataLine.erase(dataLine.begin());
            }
            if (!currentData.empty()) {
                currentData += '\n';
            }
            currentData += dataLine;
            continue;
        }

        // "id:" and "retry:" fields are acknowledged but not acted upon.
    }

#ifdef _WIN32
    _pclose(pipe);
#else
    pclose(pipe);
#endif

    // Dispatch any trailing message that was not terminated by a blank line.
    if (!currentData.empty() && onEvent) {
        onEvent(buildEvent(currentEventType, currentData));
    }

    if (running_.load() && onError) {
        onError("SSE stream closed by server");
    }
    running_.store(false);
}

// ---------------------------------------------------------------------------
// Static helpers
// ---------------------------------------------------------------------------

SseEvent EventStreamClient::buildEvent(const std::string& eventType,
                                        const std::string& data) {
    SseEvent evt;
    evt.type = eventType.empty() ? "message" : eventType;
    evt.rawData = data;

    try {
        evt.data = json::parse(data);
    } catch (...) {
        // rawData is not JSON; leave evt.data as an empty object.
        evt.data = json::object();
    }

    return evt;
}

std::string EventStreamClient::urlEncode(const std::string& value) {
    std::ostringstream encoded;
    encoded << std::hex << std::uppercase;
    for (const unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded << static_cast<char>(c);
        } else {
            encoded << '%' << static_cast<int>(c);
        }
    }
    return encoded.str();
}

} // namespace codepilot
