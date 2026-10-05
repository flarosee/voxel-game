#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include "world/Coordinates.hpp"
#include <cmath>

namespace voxel {

class Camera {
public:
    Camera() { reset(); }
    void setPosition(glm::vec3 position) { origin_={}; position_=position; rebase(); }
    void setWorldPosition(world::ChunkCoord chunk, glm::vec3 local) {
        world::validate(chunk); origin_=chunk; position_=local; rebase();
    }
    [[nodiscard]] world::ChunkCoord chunk() const noexcept { return origin_; }

    void reset() {
        origin_ = {};
        position_ = {3.0F, 2.0F, 5.0F};
        orientation_ = glm::normalize(glm::quat_cast(glm::inverse(
            glm::lookAtRH(position_, glm::vec3(0.0F), glm::vec3(0.0F, 1.0F, 0.0F)))));
    }

    // The quaternion maps camera-local axes to world axes. No Euler-angle state.
    void rotate(float yawRadians, float pitchRadians) {
        const auto yaw = glm::angleAxis(yawRadians, glm::vec3(0.0F, 1.0F, 0.0F));
        orientation_ = glm::normalize(yaw * orientation_);
        // Clamp this FPS-style camera just short of vertical. Quaternions themselves
        // allow unrestricted rotation; this limit is an intentional control choice.
        const float elevation = glm::asin(glm::clamp(forward().y, -1.0F, 1.0F));
        const float limit = glm::radians(89.0F);
        const float pitch = glm::clamp(elevation + pitchRadians, -limit, limit) - elevation;
        orientation_ = glm::normalize(orientation_ *
            glm::angleAxis(pitch, glm::vec3(1.0F, 0.0F, 0.0F)));
    }

    // x = local right, y = world up, z = local forward. Normalize diagonal motion.
    void move(glm::vec3 input, float seconds, bool fast = false) {
        glm::vec3 direction = right() * input.x + glm::vec3(0, 1, 0) * input.y + forward() * input.z;
        const float length = glm::length(direction);
        if (length > 0.0001F) {
            position_ += direction / length * (fast ? 9.0F : 3.0F) * seconds;
            rebase();
        }
    }

    [[nodiscard]] glm::mat4 view() const {
        return glm::mat4_cast(glm::conjugate(orientation_)) *
               glm::translate(glm::mat4(1.0F), -position_);
    }
    [[nodiscard]] glm::mat4 viewRelativeTo(world::ChunkCoord origin) const {
        // Subtract integers BEFORE converting to float. Nearby chunks stay precise
        // even beyond double's exact integer range.
        const glm::vec3 offset{static_cast<float>(origin_.x-origin.x)*16.0F,
            static_cast<float>(origin_.y-origin.y)*16.0F,static_cast<float>(origin_.z-origin.z)*16.0F};
        return glm::mat4_cast(glm::conjugate(orientation_)) *
            glm::translate(glm::mat4(1.0F), -(position_+offset));
    }
    [[nodiscard]] glm::vec3 forward() const { return orientation_ * glm::vec3(0, 0, -1); }
    [[nodiscard]] glm::vec3 right() const { return orientation_ * glm::vec3(1, 0, 0); }
    [[nodiscard]] glm::vec3 position() const { return position_; }
    [[nodiscard]] glm::quat orientation() const { return orientation_; }

private:
    void rebase() {
        auto next=origin_;
        auto local=position_;
        std::int64_t* components[]{&next.x,&next.y,&next.z};
        for (int axis=0;axis<3;++axis) {
            const double shift=std::floor(static_cast<double>(local[axis])/16.0);
            if (!std::isfinite(shift) || shift < -1024 || shift > 1024)
                throw std::out_of_range("Camera step too large; use setWorldPosition for teleports");
            *components[axis]+=static_cast<std::int64_t>(shift);
            local[axis]-=static_cast<float>(shift)*16.0F;
        }
        world::validate(next);
        origin_=next; position_=local;
    }
    world::ChunkCoord origin_{};
    glm::vec3 position_{};
    glm::quat orientation_{1, 0, 0, 0};
};

} // namespace voxel
