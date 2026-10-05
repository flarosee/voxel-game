#include "TerrainGenerator.hpp"
#include <array>
#include <utility>

namespace voxel::terrain {
TerrainGenerator::TerrainGenerator(std::uint64_t seed, std::shared_ptr<const Biome> grassland,
                                   std::shared_ptr<const Biome> desert)
    : seed_(seed), noise_(seed), grassland_(std::move(grassland)), desert_(std::move(desert)) {
    if (!grassland_ || !desert_) throw std::invalid_argument("Terrain biomes cannot be null");
}
Column TerrainGenerator::sampleColumn(std::int64_t x, std::int64_t z) const {
    const auto broad = noise_.sample2D(x,z,7,1);
    const auto detail = noise_.sample2D(x,z,5,2);
    const auto moisture = noise_.sample2D(x,z,8,3);
    // Geometry is continuous across biome boundaries. Biomes currently choose
    // surface/subsurface materials, not an abrupt new height function.
    const std::int64_t height = 8 + static_cast<std::int64_t>(broad) * 16 / 32768 +
                                   static_cast<std::int64_t>(detail) * 4 / 32768;
    auto result = (moisture >= 0 ? grassland_ : desert_)->column({height, moisture});
    if (result.soilDepth == 0 || result.surface == world::Air ||
        result.subsurface == world::Air || result.deep == world::Air)
        throw std::invalid_argument("Biome returned an invalid column");
    return result;
}
world::BlockId TerrainGenerator::atHeight(const Column& column, std::int64_t y) noexcept {
    if (y > column.surfaceHeight) return world::Air;
    // Unsigned subtraction gives exact nonnegative distance, including MIN -> MAX.
    const auto depth = static_cast<std::uint64_t>(column.surfaceHeight) - static_cast<std::uint64_t>(y);
    if (depth == 0) return column.surface;
    return depth < column.soilDepth ? column.subsurface : column.deep;
}
world::BlockId TerrainGenerator::sampleBlock(world::BlockCoord position) const {
    return atHeight(sampleColumn(position.x, position.z), position.y);
}
world::BlockStorage TerrainGenerator::generate(world::ChunkCoord coordinate) const {
    const auto origin = world::blockAt(coordinate, {});
    std::array<world::BlockId, world::ChunkVolume> blocks{};
    for (int z = 0; z < world::ChunkSide; ++z) {
        for (int x = 0; x < world::ChunkSide; ++x) {
            const auto column = sampleColumn(origin.x + x, origin.z + z);
            for (int y = 0; y < world::ChunkSide; ++y)
                blocks[world::indexOf({x,y,z})] = atHeight(column, origin.y + y);
        }
    }
    return world::BlockStorage::fromBlocks(blocks);
}
} // namespace voxel::terrain
