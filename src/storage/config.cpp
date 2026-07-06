#include "config.h"
#include <fstream>

namespace storage {

bool Config::load(const std::string& path) {
    std::ifstream file(path);
    if (!file) return false;
    data = json::parse(file, nullptr, false);
    return !data.is_discarded();
}

bool Config::save(const std::string& path) {
    std::ofstream file(path);
    if (!file) return false;
    file << data.dump(2);
    return true;
}

std::string Config::get(const std::string& key) const {
    return data.value(key, "");
}

void Config::set(const std::string& key, const std::string& value) {
    data[key] = value;
}

} // namespace storage
