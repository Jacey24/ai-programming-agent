#include "Game.hpp"
#include <windows.h>

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // 忽略未使用参数
    (void)hPrevInstance;
    (void)lpCmdLine;
    
    // 创建游戏实例
    Game game;
    
    // 初始化游戏
    if (!game.initialize(hInstance, nCmdShow)) {
        MessageBox(nullptr, L"游戏初始化失败！", L"错误", MB_OK | MB_ICONERROR);
        return 1;
    }
    
    // 设置定时器用于游戏更新
    SetTimer(game.getWindowHandle(), 1, 16, nullptr); // 约60 FPS
    
    // 运行游戏主循环
    return game.run();
}
