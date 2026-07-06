#pragma once

#include <string>
#include <vector>

namespace core {

struct Message {
    std::string role;    // user / assistant / tool
    std::string content;
};

class ContextManager {
public:
    void append(const Message& msg);
    std::vector<Message> build_context() const;
    int token_count() const;

private:
    std::vector<Message> history_;
};

} // namespace core
