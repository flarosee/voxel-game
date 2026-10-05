#include "TerrainWindow.hpp"

namespace voxel::terrain {
namespace {
bool same(const world::BlockStorage& a, const world::BlockStorage& b) {
    if (a.occupied() != b.occupied()) return false;
    for (std::size_t i = 0; i < world::ChunkVolume; ++i) if (a.at(i) != b.at(i)) return false;
    return true;
}
}
TerrainWindow::TerrainWindow(world::World& world, const TerrainGenerator& generator, int radius)
    : world_(world), generator_(generator), radius_(radius) {
    if (radius < 0 || radius > 2) throw std::invalid_argument("Terrain window radius must be within [0,2]");
}
std::size_t TerrainWindow::retainedEdits() const {
    std::lock_guard lock(mutex_);
    return edits_.size();
}
void TerrainWindow::update(world::ChunkCoord center) {
    world::validate(center);
    std::lock_guard lock(mutex_);
    std::unordered_set<world::ChunkCoord, world::ChunkCoordHash> wanted;
    for (int z = -radius_; z <= radius_; ++z)
        for (int y = -radius_; y <= radius_; ++y)
            for (int x = -radius_; x <= radius_; ++x) {
                // Chunk limits have ample int64 headroom for these small offsets.
                const world::ChunkCoord coordinate{center.x+x,center.y+y,center.z+z};
                if (coordinate.x < world::MinChunkCoord || coordinate.x > world::MaxChunkCoord ||
                    coordinate.y < world::MinChunkCoord || coordinate.y > world::MaxChunkCoord ||
                    coordinate.z < world::MinChunkCoord || coordinate.z > world::MaxChunkCoord) continue;
                wanted.insert(coordinate);
            }
    for (auto iterator = resident_.begin(); iterator != resident_.end();) {
        const auto coordinate = *iterator;
        if (wanted.contains(coordinate)) { ++iterator; continue; }
        const auto baseline = generator_.generate(coordinate);
        // Reserve the edit slot BEFORE unloading. Allocation failure must not lose
        // an edited chunk. Assignment of a shared_ptr after extract cannot allocate.
        auto [saved, inserted] = edits_.try_emplace(coordinate);
        (void)inserted;
        const auto removed = world_.extractChunk(coordinate);
        if (removed && !same(*removed->blocks, baseline)) saved->second = removed->blocks;
        else edits_.erase(saved);
        iterator = resident_.erase(iterator);
    }
    for (const auto coordinate : wanted) {
        // Reserve residency metadata before publishing a new chunk.
        auto [entry, inserted] = resident_.insert(coordinate);
        try {
            if (world_.snapshot(coordinate)) continue;
            const auto saved = edits_.find(coordinate);
            auto blocks = saved != edits_.end() && saved->second ? *saved->second : generator_.generate(coordinate);
            // A concurrent edit may already have allocated this coordinate; never overwrite it.
            world_.insertChunk(coordinate, std::move(blocks));
            if (saved != edits_.end()) edits_.erase(saved);
        } catch (...) {
            if (inserted) resident_.erase(entry);
            throw;
        }
    }
}
} // namespace voxel::terrain
