#pragma once

#include "World.hpp"
#include <array>

namespace voxel::world {
struct RayHit {
    BlockCoord block;
    BlockId id;
    std::optional<BlockCoord> adjacent; // Empty face-neighbor entered immediately before hit.
};

// Exact integer origin cell plus fractional position, preserving all 64-bit coordinates.
// Concurrent edits are safe; a multi-cell ray is NOT a transaction. Use compare/exchange
// when applying an edit so another writer's replacement is not accidentally destroyed.
[[nodiscard]] std::optional<RayHit> raycast(const World& world, BlockCoord origin,
    std::array<double, 3> fraction, std::array<double, 3> direction, double reach = 8.0);
} // namespace voxel::world
