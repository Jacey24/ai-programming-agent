#pragma once

#include <string>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace storage {

class Config {
public:
    bool load(const std::string& path);
    bool save(const std::string& path);

    std::string get(const std::string& key) const;
    void set(const std::string& key, const std::string& value);

    json data;
};

} // namespace storage
