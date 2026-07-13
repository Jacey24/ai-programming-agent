#ifndef RESOURCE_MANAGER_HPP
#define RESOURCE_MANAGER_HPP

#include <memory>
#include <unordered_map>
#include <string>
#include <vector>

namespace Engine {

class ResourceManager {
public:
    static ResourceManager& GetInstance();

    ResourceManager(const ResourceManager&) = delete;
    ResourceManager& operator=(const ResourceManager&) = delete;

    bool Initialize();
    void Shutdown();

    // Resource loading
    std::shared_ptr<std::vector<char>> LoadBinary(const std::string& path);
    std::string LoadText(const std::string& path);

    // Resource caching
    bool HasResource(const std::string& key) const;
    void CacheResource(const std::string& key, std::shared_ptr<void> resource);
    std::shared_ptr<void> GetResource(const std::string& key) const;

    // Directory management
    void SetResourceDirectory(const std::string& directory);
    std::string GetResourceDirectory() const;

private:
    ResourceManager() = default;
    ~ResourceManager() = default;

    std::unordered_map<std::string, std::shared_ptr<void>> m_resourceCache;
    std::string m_resourceDirectory;
    bool m_initialized = false;
};

} // namespace Engine

#endif // RESOURCE_MANAGER_HPP
