#ifndef TIMEMANAGER_HPP
#define TIMEMANAGER_HPP

#include <chrono>

class TimeManager {
public:
    TimeManager();

    // 帧率控制
    void SetTargetFPS(float fps);
    float GetTargetFPS() const;
    float GetDeltaTime() const;
    float GetRealDeltaTime() const;

    // 时间缩放
    void SetTimeScale(float scale);
    float GetTimeScale() const;

    // 帧率统计
    float GetFPS() const;
    float GetFrameTime() const;

    // 游戏时间
    float GetGameTime() const;
    float GetRealTime() const;

    // 每帧调用
    void Tick();

private:
    float targetFPS_;
    float timeScale_;
    float deltaTime_;
    float realDeltaTime_;
    float gameTime_;
    float realTime_;

    // FPS 统计
    int frameCount_;
    float fpsTimer_;
    float currentFPS_;

    std::chrono::steady_clock::time_point lastFrameTime_;
};

#endif // TIMEMANAGER_HPP
