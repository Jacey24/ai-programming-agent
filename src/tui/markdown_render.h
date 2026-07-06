#pragma once

#include <string>

namespace tui {

class MarkdownRender {
public:
    std::string render(const std::string& md);
};

} // namespace tui
