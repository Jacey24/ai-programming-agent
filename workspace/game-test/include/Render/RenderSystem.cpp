#include "RenderSystem.hpp"
#include <iostream>

namespace Engine {

struct RenderSystem::RenderData {
    float clearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    bool initialized = false;
};

RenderSystem::RenderSystem()
    : m_data(std::make_unique<RenderData>())
{
}

RenderSystem::~RenderSystem() = default;

void RenderSystem::Initialize() {
    std::cout << "[RenderSystem] Initializing..." << std::endl;
    m_data->initialized = true;
    std::cout << "[RenderSystem] Initialization complete." << std::endl;
}

void RenderSystem::Update(float deltaTime) {
    if (!m_data->initialized) return;
    // Update render logic here
}

void RenderSystem::Shutdown() {
    std::cout << "[RenderSystem] Shutting down..." << std::endl;
    m_data->initialized = false;
}

void RenderSystem::RenderFrame() {
    if (!m_data->initialized) {
        std::cerr << "[RenderSystem] Cannot render: not initialized!" << std::endl;
        return;
    }
    // Clear screen with clear color
    std::cout << "[RenderSystem] Rendering frame with clear color ("
              << m_data->clearColor[0] << ", "
              << m_data->clearColor[1] << ", "
              << m_data->clearColor[2] << ", "
              << m_data->clearColor[3] << ")" << std::endl;
}

void RenderSystem::SetClearColor(float r, float g, float b, float a) {
    m_data->clearColor[0] = r;
    m_data->clearColor[1] = g;
    m_data->clearColor[2] = b;
    m_data->clearColor[3] = a;
}

} // namespace Engine
