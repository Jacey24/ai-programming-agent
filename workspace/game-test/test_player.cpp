#include "Player.hpp"
#include <iostream>
#include <memory>

int main() {
    std::cout << "=== Testing Player Object Creation ===" << std::endl;
    
    // 创建 Player 对象
    auto player = std::make_shared<Player>("TestPlayer");
    
    // 验证对象创建成功
    if (player) {
        std::cout << "Player object created successfully!" << std::endl;
        std::cout << "Player name: " << player->GetName() << std::endl;
        std::cout << "Player active: " << (player->IsActive() ? "true" : "false") << std::endl;
        
        // 验证 Transform 组件存在
        auto transform = player->GetTransform();
        if (transform) {
            std::cout << "Transform component exists!" << std::endl;
        } else {
            std::cout << "ERROR: Transform component missing!" << std::endl;
            return 1;
        }
        
        // 测试生命周期方法
        player->Awake();
        player->Start();
        player->Update(0.016f);
        player->LateUpdate(0.016f);
        
        // 测试玩家特有方法
        std::cout << "Initial health: " << player->GetHealth() << std::endl;
        player->TakeDamage(30.0f);
        std::cout << "Health after damage: " << player->GetHealth() << std::endl;
        
        std::cout << "\n=== All tests passed! ===" << std::endl;
        return 0;
    } else {
        std::cout << "ERROR: Failed to create Player object!" << std::endl;
        return 1;
    }
}
