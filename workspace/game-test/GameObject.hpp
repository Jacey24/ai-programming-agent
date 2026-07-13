#ifndef GAMEOBJECT_HPP
#define GAMEOBJECT_HPP

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <typeindex>
#include "Component.hpp"

class TransformComponent;

class GameObject : public std::enable_shared_from_this<GameObject> {
public:
    GameObject(const std::string& name);
    ~GameObject();

    // 生命周期
    void Awake();
    void Start();
    void Update(float deltaTime);
    void LateUpdate(float deltaTime);
    void OnDestroy();

    // 组件管理
    template<typename T, typename... Args>
    T* AddComponent(Args&&... args) {
        // 检查是否已存在同类型组件
        std::type_index typeIndex(typeid(T));
        if (componentsByType_.find(typeIndex) != componentsByType_.end()) {
            return nullptr;  // 已存在，不重复添加
        }

        auto comp = std::make_shared<T>(std::forward<Args>(args)...);
        comp->SetOwner(shared_from_this());
        components_.push_back(comp);
        componentsByType_[typeIndex] = comp;
        return comp.get();
    }

    template<typename T>
    T* GetComponent() const {
        std::type_index typeIndex(typeid(T));
        auto it = componentsByType_.find(typeIndex);
        if (it != componentsByType_.end()) {
            return static_cast<T*>(it->second.get());
        }
        return nullptr;
    }

    template<typename T>
    bool RemoveComponent() {
        std::type_index typeIndex(typeid(T));
        auto it = componentsByType_.find(typeIndex);
        if (it != componentsByType_.end()) {
            auto& comp = it->second;
            comp->OnDestroy();
            components_.erase(std::remove(components_.begin(), components_.end(), comp), components_.end());
            componentsByType_.erase(it);
            return true;
        }
        return false;
    }

    // Transform 组件快捷访问
    std::shared_ptr<TransformComponent> GetTransform() const;

    // 属性
    const std::string& GetName() const;
    void SetName(const std::string& name);
    bool IsActive() const;
    void SetActive(bool active);

    // 标签系统
    void SetTag(const std::string& tag);
    const std::string& GetTag() const;
    bool CompareTag(const std::string& tag) const;

private:
    std::string name_;
    std::string tag_;
    bool active_ = true;
    bool started_ = false;

    std::vector<std::shared_ptr<Component>> components_;
    std::unordered_map<std::type_index, std::shared_ptr<Component>> componentsByType_;
    std::shared_ptr<TransformComponent> transform_;
};

#endif // GAMEOBJECT_HPP
