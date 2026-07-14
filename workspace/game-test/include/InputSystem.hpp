#ifndef INPUTSYSTEM_HPP
#define INPUTSYSTEM_HPP

#include <unordered_map>
#include <string>
#include <vector>
#include <functional>
#include "System.hpp"

// 按键状态
enum class KeyState {
    None,
    Pressed,    // 刚按下
    Held,       // 按住
    Released    // 刚释放
};

// 输入事件
struct InputEvent {
    int keyCode;
    KeyState state;
    float timestamp;
};

class InputSystem : public System {
public:
    InputSystem();

    void Update(float deltaTime, std::vector<std::shared_ptr<GameObject>>& gameObjects) override;
    std::string GetSystemName() const override;

    // 按键查询
    bool IsKeyDown(int keyCode) const;
    bool IsKeyPressed(int keyCode) const;
    bool IsKeyReleased(int keyCode) const;

    // 模拟输入（用于测试）
    void SimulateKeyPress(int keyCode);
    void SimulateKeyRelease(int keyCode);

    // 事件回调
    using KeyCallback = std::function<void(const InputEvent&)>;
    void RegisterCallback(int keyCode, KeyCallback callback);

    // 获取所有当前按下的键
    std::vector<int> GetActiveKeys() const;

private:
    std::unordered_map<int, KeyState> keyStates_;
    std::unordered_map<int, KeyCallback> callbacks_;
    std::vector<InputEvent> eventQueue_;

    void ProcessEvent(const InputEvent& event);
};

#endif // INPUTSYSTEM_HPP
