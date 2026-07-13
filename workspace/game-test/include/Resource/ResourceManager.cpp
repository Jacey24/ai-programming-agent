#include "ResourceManager.hpp"
#include <fstream>
#include <iostream>
#include <sstream>

namespace Engine {

ResourceManager& ResourceManager::GetInstance() {
    static ResourceManager instance;
    return instance;
}

bool ResourceManager::Initialize() {
    std::cout << "[ResourceManager] Initializing..." << std::endl;
    m_resourceCache.clear();
    m_initialized = true;
    std::cout << "[ResourceManager] Initialization complete." << std::endl;
    return true;
}

void ResourceManager::Shutdown() {
    std::cout << "[ResourceManager] Shutting down..." << std::endl;
    m_resourceCache.clear();
    m_initialized = false;
}

std::shared_ptr<std::vector<char>> ResourceManager::LoadBinary(const std::string& path) {
    std::string fullPath = m_resourceDirectory + "/" + path;
    std::ifstream file(fullPath, std::ios::binary | std::ios::ate);
    
    if (!file.is_open()) {
        std::cerr << "[ResourceManager] Failed to open binary file: " << fullPath << std::endl;
        return nullptr;
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    auto buffer = std::make_shared<std::vector<char>>(size);
    if (file.read(buffer->data(), size)) {
        std::cout << "[ResourceManager] Loaded binary resource: " << path << " (" << size << " bytes)" << std::endl;
        return buffer;
    }

    std::cerr << "[ResourceManager] Failed to read binary file: " << fullPath << std::endl;
    return nullptr;
}

std::string ResourceManager::LoadText(const std::string& path) {
    std::string fullPath = m_resourceDirectory + "/" + path;
    std::ifstream file(fullPath);
    
    if (!file.is_open()) {
        std::cerr << "[ResourceManager] Failed to open text file: " << fullPath << std::endl;
        return "";
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    
    std::cout << "[ResourceManager] Loaded text resource: " << path << std::endl;
    return buffer.str();
}

bool ResourceManager::HasResource(const std::string& key) const {
    return m_resourceCache.find(key) != m_resourceCache.end();
}

void ResourceManager::CacheResource(const std::string& key, std::shared_ptr<void> resource) {
    m_resourceCache[key] = resource;
    std::cout << "[ResourceManager] Cached resource: " << key << std::endl;
}

std::shared_ptr<void> ResourceManager::GetResource(const std::string& key) const {
    auto it = m_resourceCache.find(key);
    if (it != m_resourceCache.end()) {
        return it->second;
    }
    std::cerr << "[ResourceManager] Resource not found in cache: " << key << std::endl;
    return nullptr;
}

void ResourceManager::SetResourceDirectory(const std::string& directory) {
    m_resourceDirectory = directory;
    std::cout << "[ResourceManager] Resource directory set to: " << directory << std::endl;
}

std::string ResourceManager::GetResourceDirectory() const {
    return m_resourceDirectory;
}

} // namespace Engine
