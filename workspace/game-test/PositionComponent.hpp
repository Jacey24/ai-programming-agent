#ifndef POSITIONCOMPONENT_HPP
#define POSITIONCOMPONENT_HPP

#include "Component.hpp"

class PositionComponent : public Component {
public:
    PositionComponent(float x, float y);

    void Awake() override;
    void Start() override;
    void Update(float deltaTime) override;
    std::string GetType() const override;

    void SetPosition(float x, float y);
    float GetX() const;
    float GetY() const;

private:
    float x_, y_;
};

#endif // POSITIONCOMPONENT_HPP
