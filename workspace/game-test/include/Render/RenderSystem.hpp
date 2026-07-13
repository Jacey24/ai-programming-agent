#ifndef RENDER_SYSTEM_HPP
#define RENDER_SYSTEM_HPP

#include <memory>
#include <vector>
#include <string>
#include "../Core/System.hpp"

namespace Engine {

class RenderSystem : public System {
public:
    RenderSystem();
    ~RenderSystem() override;

    void Initialize() override;
    void Update(float deltaTime) override;
    void Shutdown() override;

    void RenderFrame();
    void SetClearColor(float r, float g, float b, float a);

private:
    struct RenderData;
    std::unique_ptr<RenderData> m_data;
};

} // namespace Engine

#endif // RENDER_SYSTEM_HPP
