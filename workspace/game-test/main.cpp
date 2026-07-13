#include <iostream>
#include <memory>
#include <thread>
#include <chrono>

#include "GameObject.hpp"
#include "TransformComponent.hpp"
#include "PositionComponent.hpp"
#include "RenderComponent.hpp"
#include "Scene.hpp"
#include "TimeManager.hpp"
#include "InputSystem.hpp"

// 自定义系统：移动系统
class MovementSystem : public System {
public:
    void Update(float deltaTime, std::vector<std::shared_ptr<GameObject>>& gameObjects) override {
        for (auto& obj : gameObjects) {
            if (!obj->IsActive()) continue;

            // 查找带有 PositionComponent 的对象
            auto posComp = obj->GetComponent<PositionComponent>();
            if (posComp) {
                // 简单的圆周运动
                static float time = 0.0f;
                time += deltaTime;
                float radius = 50.0f;
                float newX = 200.0f + radius * std::cos(time);
                float newY = 200.0f + radius * std::sin(time);
                posComp->SetPosition(newX, newY);
            }
        }
    }

    std::string GetSystemName() const override {
        return "MovementSystem";
    }
};

// 自定义系统：日志系统
class LogSystem : public System {
public:
    void Update(float deltaTime, std::vector<std::shared_ptr<GameObject>>& gameObjects) override {
        std::cout << "[LogSystem] Frame update - " << gameObjects.size() << " objects active" << std::endl;
    }

    std::string GetSystemName() const override {
        return "LogSystem";
    }
};

int main() {
    std::cout << "=== Game Engine Prototype ===" << std::endl;
    std::cout << "Demonstrating: Scene, Transform, Systems, Input, Time" << std::endl;
    std::cout << std::endl;

    // 1. 创建时间管理器
    TimeManager timeManager;
    timeManager.SetTargetFPS(60.0f);
    timeManager.SetTimeScale(1.0f);

    // 2. 创建主场景
    auto mainScene = std::make_shared<Scene>("MainScene");

    // 3. 注册系统
    auto* inputSystem = mainScene->AddSystem<InputSystem>();
    mainScene->AddSystem<MovementSystem>();
    mainScene->AddSystem<LogSystem>();

    // 4. 创建游戏对象
    // 4a. 玩家对象（带 Transform、Position、Render）
    auto player = mainScene->CreateGameObject("Player");
    player->SetTag("Player");
    player->AddComponent<PositionComponent>(100.0f, 100.0f);
    player->AddComponent<RenderComponent>("player_ship.png");
    auto playerTransform = player->GetTransform();
    playerTransform->SetPosition(100.0f, 100.0f);

    // 4b. 敌人对象
    auto enemy = mainScene->CreateGameObject("Enemy");
    enemy->SetTag("Enemy");
    enemy->AddComponent<PositionComponent>(300.0f, 200.0f);
    enemy->AddComponent<RenderComponent>("enemy_ship.png");
    auto enemyTransform = enemy->GetTransform();
    enemyTransform->SetPosition(300.0f, 200.0f);

    // 4c. 子对象（演示 Transform 层级）
    auto child = mainScene->CreateGameObject("ChildObject");
    child->SetTag("Child");
    child->AddComponent<RenderComponent>("child_icon.png");
    auto childTransform = child->GetTransform();
    childTransform->SetPosition(20.0f, 20.0f);  // 相对于父对象的偏移
    childTransform->SetParent(playerTransform);  // 设置为玩家的子对象

    // 5. 注册输入回调
    inputSystem->RegisterCallback(32, [](const InputEvent& event) {  // Space键
        if (event.state == KeyState::Pressed) {
            std::cout << "[InputCallback] Space pressed! Action triggered!" << std::endl;
        }
    });

    // 6. 场景初始化
    std::cout << "\n--- Scene Awake ---" << std::endl;
    mainScene->Awake();

    std::cout << "\n--- Scene Start ---" << std::endl;
    mainScene->Start();

    // 7. 模拟主循环（运行5帧）
    std::cout << "\n=== Main Loop (5 frames) ===" << std::endl;
    for (int frame = 0; frame < 5; ++frame) {
        timeManager.Tick();
        float deltaTime = timeManager.GetDeltaTime();

        std::cout << "\n--- Frame " << frame 
                  << " | FPS: " << timeManager.GetFPS()
                  << " | GameTime: " << timeManager.GetGameTime()
                  << " | Delta: " << deltaTime << " ---" << std::endl;

        // 模拟输入事件
        if (frame == 2) {
            inputSystem->SimulateKeyPress(32);  // 第2帧按下Space
        }
        if (frame == 3) {
            inputSystem->SimulateKeyRelease(32);  // 第3帧释放Space
        }

        // 更新场景
        mainScene->Update(deltaTime);

        // 显示世界坐标
        std::cout << "  Player world pos: (" 
                  << playerTransform->GetWorldX() << ", " 
                  << playerTransform->GetWorldY() << ")" << std::endl;
        std::cout << "  Child world pos: (" 
                  << childTransform->GetWorldX() << ", " 
                  << childTransform->GetWorldY() << ")" << std::endl;

        // 模拟帧率
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    // 8. 清理
    std::cout << "\n=== Cleanup ===" << std::endl;
    mainScene->DestroyGameObject(child);
    mainScene->DestroyGameObject(enemy);
    mainScene->DestroyGameObject(player);

    std::cout << "\nGame engine prototype running successfully!" << std::endl;
    return 0;
}
