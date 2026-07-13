#include "GameObject.hpp"
#include "TransformComponent.hpp"
#include <iostream>
#include <algorithm>

GameObject::GameObject(const std::string& name) : name_(name) {
    // 每个 GameObject 默认拥有一个 Transform 组件
    transform_ = std::make_shared<TransformComponent>();
    transform_->SetOwner(shared_from_this());
    components_.push_back(transform_);
    componentsByType_[std::type_index(typeid(TransformComponent))] = transform_;
}

GameObject::~GameObject() {
    OnDestroy();
}

void GameObject::Awake() {
    if (!active_) return;
    for (auto& comp : components_) {
        comp->Awake();
    }
}

void GameObject::Start() {
    if (!active_ || started_) return;
    for (auto& comp : components_) {
        comp->Start();
    }
    started_ = true;
}

void GameObject::Update(float deltaTime) {
    if (!active_) return;
    for (auto& comp : components_) {
        if (comp->IsEnabled()) {
            comp->Update(deltaTime);
        }
    }
}

void GameObject::LateUpdate(float deltaTime) {
    if (!active_) return;
    for (auto& comp : components_) {
        if (comp->IsEnabled()) {
            comp->LateUpdate(deltaTime);
        }
    }
}

void GameObject::OnDestroy() {
    for (auto& comp : components_) {
        comp->OnDestroy();
    }
    components_.clear();
    componentsByType_.clear();
}

std::shared_ptr<TransformComponent> GameObject::GetTransform() const {
    return transform_;
}

const std::string& GameObject::GetName() const {
    return name_;
}

void GameObject::SetName(const std::string& name) {
    name_ = name;
}

bool GameObject::IsActive() const {
    return active_;
}

void GameObject::SetActive(bool active) {
    active_ = active;
}

void GameObject::SetTag(const std::string& tag) {
    tag_ = tag;
}

const std::string& GameObject::GetTag() const {
    return tag_;
}

bool GameObject::CompareTag(const std::string& tag) const {
    return tag_ == tag;
}
