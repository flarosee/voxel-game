#pragma once

#include "TerrainGenerator.hpp"
#include "world/World.hpp"
#include <mutex>
#include <unordered_set>

namespace voxel::terrain {
// One residency controller per World. update() calls are serialized; generation
// itself is pure and can be used independently by concurrent worker jobs.
class TerrainWindow final {
public:
    TerrainWindow(world::World& world, const TerrainGenerator& generator, int radius = 1);
    void update(world::ChunkCoord center);
    [[nodiscard]] std::size_t retainedEdits() const;
private:
    world::World& world_;
    const TerrainGenerator& generator_;
    const int radius_;
    mutable std::mutex mutex_;
    std::unordered_set<world::ChunkCoord, world::ChunkCoordHash> resident_;
    std::unordered_map<world::ChunkCoord, std::shared_ptr<const world::BlockStorage>, world::ChunkCoordHash> edits_;
};
} // namespace voxel::terrain
