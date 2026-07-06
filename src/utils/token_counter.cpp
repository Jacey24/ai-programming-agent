#include "token_counter.h"

namespace utils {

int TokenCounter::count(const std::string& text) {
    // TODO: Implement tiktoken-equivalent algorithm or call API
    // Rough approximation: ~4 chars per token for English
    return static_cast<int>(text.size() / 4);
}

} // namespace utils
