#include "context.h"

namespace core {

void ContextManager::append(const Message& msg) {
    history_.push_back(msg);
}

std::vector<Message> ContextManager::build_context() const {
    // TODO: Sliding window + summarization for old messages
    return history_;
}

int ContextManager::token_count() const {
    // TODO: tiktoken-equivalent counting
    return 0;
}

} // namespace core
