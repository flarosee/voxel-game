#pragma once

#include "../Types.hpp"

#include "Biome.hpp"
#include "Noise.hpp"
#include "world/BlockStorage.hpp"
#include <memory>

namespace voxel::terrain {
class TerrainGenerator final {
public:
    static constexpr std::uint32_t Version = 1;
    static constexpr UInt64 DefaultSeed = 12345;
    explicit TerrainGenerator(UInt64 seed = DefaultSeed,
        std::shared_ptr<const Biome> grassland = std::make_shared<GrasslandBiome>(),
        std::shared_ptr<const Biome> desert = std::make_shared<DesertBiome>());
    [[nodiscard]] UInt64 seed() const noexcept { return seed_; }
    [[nodiscard]] Column sampleColumn(std::int64_t x, std::int64_t z) const;
    [[nodiscard]] world::BlockId sampleBlock(world::BlockCoord position) const;
    [[nodiscard]] world::BlockStorage generate(world::ChunkCoord coordinate) const;
private:
    static world::BlockId atHeight(const Column& column, std::int64_t y) noexcept;
    const UInt64 seed_;
    const Noise noise_;
    const std::shared_ptr<const Biome> grassland_;
    const std::shared_ptr<const Biome> desert_;
};
} // namespace voxel::terrain
