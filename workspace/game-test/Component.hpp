#ifndef COMPONENT_HPP
#define COMPONENT_HPP

#include <string>
#include <memory>

class GameObject;

class Component : public std::enable_shared_from_this<Component> {
public:
    Component() : owner_(nullptr), enabled_(true) {}
    virtual ~Component() = default;

    // 生命周期方法
    virtual void Awake() {}
    virtual void Start() {}
    virtual void Update(float deltaTime) {}
    virtual void LateUpdate(float deltaTime) {}
    virtual void OnDestroy() {}

    // 组件类型标识
    virtual std::string GetType() const = 0;

    // 所属游戏对象
    void SetOwner(std::shared_ptr<GameObject> owner) { owner_ = owner; }
    std::shared_ptr<GameObject> GetOwner() const { return owner_.lock(); }

    // 启用/禁用
    void SetEnabled(bool enabled) { enabled_ = enabled; }
    bool IsEnabled() const { return enabled_; }

private:
    std::weak_ptr<GameObject> owner_;
    bool enabled_;
};

#endif // COMPONENT_HPP
