#include "TransformComponent.hpp"
#include <iostream>

TransformComponent::TransformComponent(float x, float y, float rotation, float scaleX, float scaleY)
    : x_(x), y_(y), rotation_(rotation), scaleX_(scaleX), scaleY_(scaleY) {}

void TransformComponent::Awake() {
    // 初始化逻辑
}

void TransformComponent::Start() {
    // 启动逻辑
}

void TransformComponent::Update(float deltaTime) {
    // 变换组件本身不需要每帧更新，但可以用于动画插值
}

std::string TransformComponent::GetType() const {
    return "TransformComponent";
}

void TransformComponent::SetPosition(float x, float y) {
    x_ = x;
    y_ = y;
}

void TransformComponent::Translate(float dx, float dy) {
    x_ += dx;
    y_ += dy;
}

float TransformComponent::GetX() const { return x_; }
float TransformComponent::GetY() const { return y_; }

void TransformComponent::SetRotation(float degrees) {
    rotation_ = fmod(degrees, 360.0f);
    if (rotation_ < 0) rotation_ += 360.0f;
}

void TransformComponent::Rotate(float degrees) {
    SetRotation(rotation_ + degrees);
}

float TransformComponent::GetRotation() const { return rotation_; }

void TransformComponent::SetScale(float sx, float sy) {
    scaleX_ = sx;
    scaleY_ = sy;
}

float TransformComponent::GetScaleX() const { return scaleX_; }
float TransformComponent::GetScaleY() const { return scaleY_; }

void TransformComponent::SetParent(std::shared_ptr<TransformComponent> parent) {
    parent_ = parent;
    if (auto p = parent_.lock()) {
        p->AddChild(shared_from_this());
    }
}

std::shared_ptr<TransformComponent> TransformComponent::GetParent() const {
    return parent_.lock();
}

void TransformComponent::AddChild(std::shared_ptr<TransformComponent> child) {
    if (std::find(children_.begin(), children_.end(), child) == children_.end()) {
        children_.push_back(child);
    }
}

const std::vector<std::shared_ptr<TransformComponent>>& TransformComponent::GetChildren() const {
    return children_;
}

float TransformComponent::GetWorldX() const {
    if (auto parent = parent_.lock()) {
        return parent->GetWorldX() + x_;
    }
    return x_;
}

float TransformComponent::GetWorldY() const {
    if (auto parent = parent_.lock()) {
        return parent->GetWorldY() + y_;
    }
    return y_;
}

float TransformComponent::GetWorldRotation() const {
    if (auto parent = parent_.lock()) {
        return parent->GetWorldRotation() + rotation_;
    }
    return rotation_;
}
