#pragma once
#include "Coordinates.hpp"

namespace voxel::world::blocks {
// IDs 1..5 are the existing terrain materials. Keep persisted IDs stable.
inline constexpr BlockId Lamp = 6;
inline constexpr BlockId Water = 7, Glass = 8, Leaves = 9;
inline constexpr bool blended(BlockId id) noexcept { return id==Water || id==Glass; }
inline constexpr bool cutout(BlockId id) noexcept { return id==Leaves; }
inline constexpr bool opaque(BlockId id) noexcept { return id!=Air && !blended(id) && !cutout(id); }
// Lighting uses voxel transmission, independent of the leaf shader's tiny holes.
inline constexpr bool transmitsLight(BlockId id) noexcept { return !opaque(id); }
inline constexpr bool faceVisible(BlockId id, BlockId neighbor) noexcept {
    if (id==Air || opaque(neighbor)) return false;
    if (opaque(id) || neighbor==Air) return true;
    if (id==neighbor) return false;
    // Leaves own their cutout surface; the blend behind its holes is also needed.
    if (cutout(id) || cutout(neighbor)) return true;
    // One double-sided interface between different blended media, no coplanar blend.
    return id>neighbor;
}
inline constexpr std::uint8_t emission(BlockId id) noexcept { return id==Lamp ? 15 : 0; }
} // namespace voxel::world::blocks
