#pragma once

#include "Biome.hpp"
#include "Noise.hpp"
#include "world/BlockStorage.hpp"
#include <memory>

namespace voxel::terrain {
class TerrainGenerator final {
public:
    static constexpr std::uint32_t Version = 1;
    static constexpr std::uint64_t DefaultSeed = 12345;
    explicit TerrainGenerator(std::uint64_t seed = DefaultSeed,
        std::shared_ptr<const Biome> grassland = std::make_shared<GrasslandBiome>(),
        std::shared_ptr<const Biome> desert = std::make_shared<DesertBiome>());
    [[nodiscard]] std::uint64_t seed() const noexcept { return seed_; }
    [[nodiscard]] Column sampleColumn(std::int64_t x, std::int64_t z) const;
    [[nodiscard]] world::BlockId sampleBlock(world::BlockCoord position) const;
    [[nodiscard]] world::BlockStorage generate(world::ChunkCoord coordinate) const;
private:
    static world::BlockId atHeight(const Column& column, std::int64_t y) noexcept;
    const std::uint64_t seed_;
    const Noise noise_;
    const std::shared_ptr<const Biome> grassland_;
    const std::shared_ptr<const Biome> desert_;
};
} // namespace voxel::terrain
