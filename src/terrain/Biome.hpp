#pragma once

#include "world/Coordinates.hpp"
#include <string_view>

namespace voxel::terrain {
namespace blocks {
inline constexpr world::BlockId Grass = 1, Dirt = 2, Stone = 3, Sand = 4, Sandstone = 5;
}
struct TerrainSample {
    std::int64_t surfaceHeight;
    std::int32_t moisture; // [-32768,32767].
};
struct Column {
    std::int64_t surfaceHeight;
    world::BlockId surface;
    world::BlockId subsurface;
    world::BlockId deep;
    std::uint16_t soilDepth; // Includes the surface block; must be >= 1.
    bool operator==(const Column&) const = default;
};

// Implementations must be immutable, reentrant, and deterministic. No RNG state,
// chunk-local coordinates, clock, renderer, or mutable World belongs in this interface.
class Biome {
public:
    virtual ~Biome() = default;
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    [[nodiscard]] virtual Column column(const TerrainSample& sample) const = 0;
};
class GrasslandBiome final : public Biome {
public:
    std::string_view name() const noexcept override { return "Grassland"; }
    Column column(const TerrainSample& sample) const override {
        return {sample.surfaceHeight, blocks::Grass, blocks::Dirt, blocks::Stone, 4};
    }
};
class DesertBiome final : public Biome {
public:
    std::string_view name() const noexcept override { return "Desert"; }
    Column column(const TerrainSample& sample) const override {
        return {sample.surfaceHeight, blocks::Sand, blocks::Sandstone, blocks::Stone, 6};
    }
};
} // namespace voxel::terrain
