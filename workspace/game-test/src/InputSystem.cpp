#include "InputSystem.hpp"
#include <iostream>

InputSystem::InputSystem() {}

void InputSystem::Update(float deltaTime, std::vector<std::shared_ptr<GameObject>>& gameObjects) {
    // 处理事件队列
    for (const auto& event : eventQueue_) {
        ProcessEvent(event);
    }
    eventQueue_.clear();

    // 更新按键状态：Pressed -> Held
    for (auto& [key, state] : keyStates_) {
        if (state == KeyState::Pressed) {
            state = KeyState::Held;
        } else if (state == KeyState::Released) {
            state = KeyState::None;
        }
    }
}

std::string InputSystem::GetSystemName() const {
    return "InputSystem";
}

bool InputSystem::IsKeyDown(int keyCode) const {
    auto it = keyStates_.find(keyCode);
    if (it != keyStates_.end()) {
        return it->second == KeyState::Pressed || it->second == KeyState::Held;
    }
    return false;
}

bool InputSystem::IsKeyPressed(int keyCode) const {
    auto it = keyStates_.find(keyCode);
    if (it != keyStates_.end()) {
        return it->second == KeyState::Pressed;
    }
    return false;
}

bool InputSystem::IsKeyReleased(int keyCode) const {
    auto it = keyStates_.find(keyCode);
    if (it != keyStates_.end()) {
        return it->second == KeyState::Released;
    }
    return false;
}

void InputSystem::SimulateKeyPress(int keyCode) {
    InputEvent event;
    event.keyCode = keyCode;
    event.state = KeyState::Pressed;
    event.timestamp = 0.0f;  // 简化处理
    eventQueue_.push_back(event);
}

void InputSystem::SimulateKeyRelease(int keyCode) {
    InputEvent event;
    event.keyCode = keyCode;
    event.state = KeyState::Released;
    event.timestamp = 0.0f;
    eventQueue_.push_back(event);
}

void InputSystem::RegisterCallback(int keyCode, KeyCallback callback) {
    callbacks_[keyCode] = callback;
}

std::vector<int> InputSystem::GetActiveKeys() const {
    std::vector<int> activeKeys;
    for (const auto& [key, state] : keyStates_) {
        if (state == KeyState::Pressed || state == KeyState::Held) {
            activeKeys.push_back(key);
        }
    }
    return activeKeys;
}

void InputSystem::ProcessEvent(const InputEvent& event) {
    keyStates_[event.keyCode] = event.state;

    // 触发回调
    auto it = callbacks_.find(event.keyCode);
    if (it != callbacks_.end()) {
        it->second(event);
    }

    // 打印输入事件
    std::string stateStr;
    switch (event.state) {
        case KeyState::Pressed:  stateStr = "Pressed"; break;
        case KeyState::Released: stateStr = "Released"; break;
        default: stateStr = "Unknown"; break;
    }
    std::cout << "[InputSystem] Key " << event.keyCode << " " << stateStr << std::endl;
}
