#ifndef TRANSFORMCOMPONENT_HPP
#define TRANSFORMCOMPONENT_HPP

#include "Component.hpp"
#include <memory>
#include <cmath>

class TransformComponent : public Component {
public:
    TransformComponent(float x = 0.0f, float y = 0.0f, float rotation = 0.0f, float scaleX = 1.0f, float scaleY = 1.0f);

    // 生命周期
    void Awake() override;
    void Start() override;
    void Update(float deltaTime) override;
    std::string GetType() const override;

    // 位置
    void SetPosition(float x, float y);
    void Translate(float dx, float dy);
    float GetX() const;
    float GetY() const;

    // 旋转
    void SetRotation(float degrees);
    void Rotate(float degrees);
    float GetRotation() const;

    // 缩放
    void SetScale(float sx, float sy);
    float GetScaleX() const;
    float GetScaleY() const;

    // 层级变换（父子关系）
    void SetParent(std::shared_ptr<TransformComponent> parent);
    std::shared_ptr<TransformComponent> GetParent() const;
    void AddChild(std::shared_ptr<TransformComponent> child);
    const std::vector<std::shared_ptr<TransformComponent>>& GetChildren() const;

    // 获取世界坐标（递归计算）
    float GetWorldX() const;
    float GetWorldY() const;
    float GetWorldRotation() const;

private:
    float x_, y_;
    float rotation_;  // 度
    float scaleX_, scaleY_;

    std::weak_ptr<TransformComponent> parent_;
    std::vector<std::shared_ptr<TransformComponent>> children_;
};

#endif // TRANSFORMCOMPONENT_HPP
