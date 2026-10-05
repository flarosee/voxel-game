#pragma once
#include "terrain/TerrainGenerator.hpp"
#include "world/Chunk.hpp"
#include <array>
#include <functional>
#include <optional>
#include <span>
#include <unordered_map>
#include <unordered_set>

namespace voxel::lighting {
inline constexpr std::uint8_t FullSunlight = 15;

// Direct vertical skylight only. One highest opaque block per X/Z column;
// no per-voxel light allocation, lateral flood fill, block lights, or AO here.
// Build on one worker, then publish as const alongside the geometry snapshots.
class Sunlight final {
public:
    using OverrideReader = std::function<std::optional<world::ChunkSnapshot>(world::ChunkCoord)>;
    using Cancelled = std::function<bool()>;
    // Resolver must return edited/resident data, or nullopt for procedural data.
    // Captures only the horizontal footprint of snapshots. nullopt = cancelled.
    // A previous cache must use the same generator/override world. The caller
    // must invalidate every column whose opacity changed before reusing it.
    static std::optional<Sunlight> terrain(const terrain::TerrainGenerator& generator,
        std::span<const world::ChunkSnapshot> snapshots, const OverrideReader& read,
        const Cancelled& cancelled, const Sunlight* previous = nullptr);
    // Generation-worker invalidation. Published copies remain immutable.
    void invalidate(world::BlockCoord block);
    void invalidateChunk(world::ChunkCoord coordinate);
    [[nodiscard]] std::size_t recomputedColumns() const noexcept { return recomputed_; }
    // Fold a chunk's opaque cells into known columns. Used for offscreen roofs.
    // For standalone finite fixtures, first call add() to define their columns.
    void add(const world::ChunkSnapshot& snapshot);
    void include(const world::ChunkSnapshot& snapshot);
    [[nodiscard]] bool contains(world::ChunkCoord coordinate) const;
    [[nodiscard]] std::uint8_t face(world::BlockCoord block, int axis, int sign, world::BlockId material=3) const;
    [[nodiscard]] std::size_t columnTiles() const noexcept { return tops_.size(); }
private:
    using Height = std::optional<std::int64_t>;
    using Tile = std::array<Height,world::ChunkSide*world::ChunkSide>;
    std::unordered_map<world::ChunkCoord,Tile,world::ChunkCoordHash> tops_;
    // Hash keys store absolute X/Z block coordinates, not actual chunk addresses.
    std::unordered_set<world::ChunkCoord,world::ChunkCoordHash> invalid_;
    std::size_t recomputed_ = 0;
};
} // namespace voxel::lighting
