#ifndef RENDERCOMPONENT_HPP
#define RENDERCOMPONENT_HPP

#include <string>
#include "Component.hpp"

class RenderComponent : public Component {
public:
    RenderComponent(const std::string& sprite);

    void Awake() override;
    void Start() override;
    void Update(float deltaTime) override;
    std::string GetType() const override;

    void SetSprite(const std::string& sprite);
    const std::string& GetSprite() const;
    void SetVisible(bool visible);
    bool IsVisible() const;

    // 渲染层
    void SetRenderLayer(int layer);
    int GetRenderLayer() const;

private:
    std::string sprite_;
    bool visible_ = true;
    int renderLayer_ = 0;
};

#endif // RENDERCOMPONENT_HPP
