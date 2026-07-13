#ifndef SYSTEM_HPP
#define SYSTEM_HPP

#include <vector>
#include <memory>
#include "GameObject.hpp"

class System {
public:
    virtual ~System() = default;
    virtual void Update(float deltaTime, std::vector<std::shared_ptr<GameObject>>& gameObjects) = 0;
    virtual std::string GetSystemName() const = 0;
};

#endif // SYSTEM_HPP
