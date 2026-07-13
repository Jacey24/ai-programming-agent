#include "Player.hpp"
#include <iostream>

Player::Player(const std::string& name) : GameObject(name) {
    std::cout << "Player \"" << name << "\" created." << std::endl;
}

Player::~Player() {
    OnDestroy();
}

void Player::Awake() {
    GameObject::Awake();
    std::cout << "Player::Awake() - " << GetName() << std::endl;
}

void Player::Start() {
    GameObject::Start();
    std::cout << "Player::Start() - " << GetName() << std::endl;
}

void Player::Update(float deltaTime) {
    GameObject::Update(deltaTime);
    // 玩家更新逻辑
}

void Player::LateUpdate(float deltaTime) {
    GameObject::LateUpdate(deltaTime);
    // 玩家延迟更新逻辑
}

void Player::OnDestroy() {
    GameObject::OnDestroy();
    std::cout << "Player::OnDestroy() - " << GetName() << std::endl;
}

float Player::GetHealth() const {
    return health_;
}

void Player::SetHealth(float health) {
    health_ = health;
    if (health_ <= 0.0f) {
        isAlive_ = false;
        std::cout << GetName() << " has died." << std::endl;
    }
}

float Player::GetSpeed() const {
    return speed_;
}

void Player::SetSpeed(float speed) {
    speed_ = speed;
}

void Player::Move(float dx, float dy, float dz) {
    auto transform = GetTransform();
    if (transform) {
        // 假设 TransformComponent 有移动方法
        // transform->Translate(dx * speed_, dy * speed_, dz * speed_);
        std::cout << GetName() << " moving by (" << dx << ", " << dy << ", " << dz << ")" << std::endl;
    }
}

void Player::TakeDamage(float damage) {
    health_ -= damage;
    std::cout << GetName() << " took " << damage << " damage. Health: " << health_ << std::endl;
    if (health_ <= 0.0f) {
        isAlive_ = false;
        std::cout << GetName() << " has died." << std::endl;
    }
}
