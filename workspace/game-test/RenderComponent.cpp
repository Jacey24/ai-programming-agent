#include "RenderComponent.hpp"
#include <iostream>

RenderComponent::RenderComponent(const std::string& sprite) : sprite_(sprite) {}

void RenderComponent::Update(float deltaTime) {
    std::cout << "RenderComponent updated, sprite: " << sprite_ << std::endl;
}

std::string RenderComponent::GetType() const {
    return "RenderComponent";
}
