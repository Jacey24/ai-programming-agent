#pragma once

#include <string>
#include <vector>
#include "context.h"

namespace core {

class LlmClient {
public:
    std::string chat(const std::vector<Message>& context, const std::string& tools_schema);
};

} // namespace core
