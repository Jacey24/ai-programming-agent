#include "TimeManager.hpp"
#include <thread>

TimeManager::TimeManager()
    : targetFPS_(60.0f)
    , timeScale_(1.0f)
    , deltaTime_(0.0f)
    , realDeltaTime_(0.0f)
    , gameTime_(0.0f)
    , realTime_(0.0f)
    , frameCount_(0)
    , fpsTimer_(0.0f)
    , currentFPS_(0.0f)
    , lastFrameTime_(std::chrono::steady_clock::now())
{}

void TimeManager::SetTargetFPS(float fps) {
    targetFPS_ = fps;
}

float TimeManager::GetTargetFPS() const {
    return targetFPS_;
}

float TimeManager::GetDeltaTime() const {
    return deltaTime_;
}

float TimeManager::GetRealDeltaTime() const {
    return realDeltaTime_;
}

void TimeManager::SetTimeScale(float scale) {
    timeScale_ = scale;
}

float TimeManager::GetTimeScale() const {
    return timeScale_;
}

float TimeManager::GetFPS() const {
    return currentFPS_;
}

float TimeManager::GetFrameTime() const {
    return realDeltaTime_;
}

float TimeManager::GetGameTime() const {
    return gameTime_;
}

float TimeManager::GetRealTime() const {
    return realTime_;
}

void TimeManager::Tick() {
    auto now = std::chrono::steady_clock::now();
    std::chrono::duration<float> elapsed = now - lastFrameTime_;
    lastFrameTime_ = now;

    realDeltaTime_ = elapsed.count();

    // 帧率限制
    float targetFrameTime = 1.0f / targetFPS_;
    if (realDeltaTime_ < targetFrameTime) {
        std::this_thread::sleep_for(std::chrono::duration<float>(targetFrameTime - realDeltaTime_));
        now = std::chrono::steady_clock::now();
        elapsed = now - lastFrameTime_;
        realDeltaTime_ = elapsed.count();
        lastFrameTime_ = now;
    }

    // 应用时间缩放
    deltaTime_ = realDeltaTime_ * timeScale_;

    // 更新时间
    gameTime_ += deltaTime_;
    realTime_ += realDeltaTime_;

    // FPS 统计
    frameCount_++;
    fpsTimer_ += realDeltaTime_;
    if (fpsTimer_ >= 1.0f) {
        currentFPS_ = frameCount_ / fpsTimer_;
        frameCount_ = 0;
        fpsTimer_ = 0.0f;
    }
}
