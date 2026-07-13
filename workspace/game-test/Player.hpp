#ifndef PLAYER_HPP
#define PLAYER_HPP

#include "GameObject.hpp"
#include "TransformComponent.hpp"

class Player : public GameObject {
public:
    Player(const std::string& name = "Player");
    ~Player();

    // 生命周期方法
    void Awake() override;
    void Start() override;
    void Update(float deltaTime) override;
    void LateUpdate(float deltaTime) override;
    void OnDestroy() override;

    // 玩家特有属性
    float GetHealth() const;
    void SetHealth(float health);
    float GetSpeed() const;
    void SetSpeed(float speed);

    // 玩家行为
    void Move(float dx, float dy, float dz);
    void TakeDamage(float damage);

private:
    float health_ = 100.0f;
    float speed_ = 5.0f;
    bool isAlive_ = true;
};

#endif // PLAYER_HPP
