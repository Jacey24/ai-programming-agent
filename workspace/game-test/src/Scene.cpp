#include "Scene.hpp"
#include <iostream>

Scene::Scene(const std::string& name) : name_(name) {}

Scene::~Scene() {}

const std::string& Scene::GetName() const {
    return name_;
}

std::shared_ptr<GameObject> Scene::CreateGameObject(const std::string& name) {
    auto obj = std::make_shared<GameObject>(name);
    gameObjects_.push_back(obj);
    return obj;
}

void Scene::DestroyGameObject(std::shared_ptr<GameObject> obj) {
    auto it = std::find(gameObjects_.begin(), gameObjects_.end(), obj);
    if (it != gameObjects_.end()) {
        gameObjects_.erase(it);
    }
}

std::shared_ptr<GameObject> Scene::FindGameObjectByName(const std::string& name) const {
    for (const auto& obj : gameObjects_) {
        if (obj->GetName() == name) {
            return obj;
        }
    }
    return nullptr;
}

const std::vector<std::shared_ptr<GameObject>>& Scene::GetAllGameObjects() const {
    return gameObjects_;
}

void Scene::Awake() {
    for (auto& obj : gameObjects_) {
        obj->Awake();
    }
}

void Scene::Start() {
    for (auto& obj : gameObjects_) {
        obj->Start();
    }
    started_ = true;
}

void Scene::Update(float deltaTime) {
    // 更新所有游戏对象
    for (auto& obj : gameObjects_) {
        obj->Update(deltaTime);
    }

    // 更新所有系统
    for (auto& sys : systems_) {
        sys->Update(deltaTime, gameObjects_);
    }
}
