#ifndef SCENE_HPP
#define SCENE_HPP

#include <vector>
#include <memory>
#include <string>
#include <algorithm>
#include "GameObject.hpp"
#include "System.hpp"

class Scene {
public:
    Scene(const std::string& name);
    ~Scene();

    const std::string& GetName() const;

    // 游戏对象管理
    std::shared_ptr<GameObject> CreateGameObject(const std::string& name);
    void DestroyGameObject(std::shared_ptr<GameObject> obj);
    std::shared_ptr<GameObject> FindGameObjectByName(const std::string& name) const;
    const std::vector<std::shared_ptr<GameObject>>& GetAllGameObjects() const;

    // 系统管理
    template<typename T, typename... Args>
    T* AddSystem(Args&&... args) {
        auto sys = std::make_shared<T>(std::forward<Args>(args)...);
        systems_.push_back(sys);
        return sys.get();
    }

    // 生命周期
    void Awake();
    void Start();
    void Update(float deltaTime);

private:
    std::string name_;
    std::vector<std::shared_ptr<GameObject>> gameObjects_;
    std::vector<std::shared_ptr<System>> systems_;
    bool started_ = false;
};

#endif // SCENE_HPP
