#include "PositionComponent.hpp"
#include <iostream>

PositionComponent::PositionComponent(float x, float y) : x_(x), y_(y) {}

void PositionComponent::Update(float deltaTime) {
    std::cout << "PositionComponent updated, pos: (" << x_ << ", " << y_ << ")" << std::endl;
}

std::string PositionComponent::GetType() const {
    return "PositionComponent";
}
