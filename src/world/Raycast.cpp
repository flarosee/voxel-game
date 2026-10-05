#include "Raycast.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace voxel::world {
std::optional<RayHit> raycast(const World& world, BlockCoord origin,
    std::array<double, 3> fraction, std::array<double, 3> direction, double reach) {
    if (!std::isfinite(reach) || reach < 0 || reach > 1024)
        throw std::invalid_argument("Ray reach must be finite and within [0, 1024]");
    for (std::size_t axis = 0; axis < 3; ++axis) {
        if (!std::isfinite(fraction[axis]) || fraction[axis] < 0 || fraction[axis] >= 1 ||
            !std::isfinite(direction[axis])) throw std::invalid_argument("Invalid ray");
    }
    const auto magnitude = std::hypot(direction[0], direction[1], direction[2]);
    if (!std::isfinite(magnitude) || magnitude == 0) throw std::invalid_argument("Invalid ray direction");
    std::array<std::int64_t, 3> cell{origin.x, origin.y, origin.z};
    std::array<int, 3> step{};
    std::array<double, 3> next{}, delta{};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        direction[axis] /= magnitude;
        step[axis] = direction[axis] > 0 ? 1 : direction[axis] < 0 ? -1 : 0;
        delta[axis] = step[axis] == 0 ? std::numeric_limits<double>::infinity() : 1.0 / std::abs(direction[axis]);
        next[axis] = step[axis] > 0 ? (1.0 - fraction[axis]) * delta[axis] :
                     step[axis] < 0 ? fraction[axis] * delta[axis] : delta[axis];
    }
    std::optional<BlockCoord> previous;
    for (;;) {
        const BlockCoord current{cell[0], cell[1], cell[2]};
        const auto id = world.getBlock(current);
        if (id != Air) return RayHit{current, id, previous};
        const auto axis = static_cast<std::size_t>(std::min_element(next.begin(), next.end()) - next.begin());
        if (next[axis] > reach) return std::nullopt;
        if ((step[axis] > 0 && cell[axis] == std::numeric_limits<std::int64_t>::max()) ||
            (step[axis] < 0 && cell[axis] == std::numeric_limits<std::int64_t>::min())) return std::nullopt;
        previous = current;
        cell[axis] += step[axis];
        next[axis] += delta[axis];
        // Equal-axis crossings resolve X, then Y, then Z, deterministically.
    }
}
} // namespace voxel::world
