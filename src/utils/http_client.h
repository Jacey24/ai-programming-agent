#pragma once

#include <string>

namespace utils {

class HttpClient {
public:
    static std::string post(const std::string& url, const std::string& body,
                            const std::string& api_key = "");
    static std::string get(const std::string& url);
};

} // namespace utils
