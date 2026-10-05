#include "Camera.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main() {
    try {
        voxel::Camera camera;
        const auto eye = camera.view() * glm::vec4(camera.position(), 1);
        require(glm::length(glm::vec3(eye)) < 0.0001F, "View must map camera position to origin");
        const auto target = camera.view() * glm::vec4(0, 0, 0, 1);
        require(std::abs(target.x) < 0.0001F && std::abs(target.y) < 0.0001F && target.z < 0,
                "Initial camera must face cube center");
        const auto forward = camera.forward();
        camera.rotate(glm::radians(90.0F), 0);
        const glm::vec3 expected(forward.z, forward.y, -forward.x);
        require(glm::length(camera.forward() - expected) < 0.0001F, "Yaw must rotate around world up");
        camera.rotate(0, 100.0F);
        require(std::abs(camera.forward().y - std::sin(glm::radians(89.0F))) < 0.0001F,
                "Pitch must clamp at 89 degrees");
        for (int i = 0; i < 10000; ++i) camera.rotate(0.002F, -0.0001F);
        require(std::abs(glm::length(camera.orientation()) - 1.0F) < 0.0001F,
                "Repeated rotations must retain a unit quaternion");
        require(std::abs(glm::dot(camera.forward(), camera.right())) < 0.0001F,
                "Camera axes must stay orthogonal");
        camera.reset();
        auto start = camera.position();
        camera.move({1, 0, 1}, 1.0F);
        require(std::abs(glm::length(camera.position() - start) - 3.0F) < 0.0001F,
                "Diagonal movement must not increase speed");
        voxel::Camera split;
        for (int i = 0; i < 100; ++i) split.move({1, 0, 1}, 0.01F);
        require(glm::length(split.position() - camera.position()) < 0.0001F,
                "Movement must be independent of frame rate");
        const voxel::world::ChunkCoord distant{INT64_C(1)<<50,-(INT64_C(1)<<50),17};
        camera.setWorldPosition(distant,{15.9F,0.1F,8});
        camera.move({0,-1,0},0.1F);
        require(camera.chunk().y==distant.y-1 && camera.position().y>15.0F,"Negative crossing must rebase exactly");
        const auto distantEye=camera.viewRelativeTo(camera.chunk())*glm::vec4(camera.position(),1);
        require(glm::length(glm::vec3(distantEye))<0.0001F,"Huge coordinates must preserve local camera precision");
        const auto neighbor=voxel::world::ChunkCoord{camera.chunk().x+1,camera.chunk().y,camera.chunk().z};
        const auto fromNeighbor=camera.viewRelativeTo(neighbor)*glm::vec4(camera.position()-glm::vec3(16,0,0),1);
        require(glm::length(glm::vec3(fromNeighbor))<0.0001F,"Camera and mesh origins must agree across a seam");
        std::cout << "Quaternion camera checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
