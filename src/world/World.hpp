#pragma once

#include "Chunk.hpp"
#include <optional>
#include <unordered_map>
#include <vector>

namespace voxel::world {

class World final {
public:
    bool allocateChunk(ChunkCoord coordinate, BlockId fill = Air);
    bool eraseChunk(ChunkCoord coordinate);
    // Atomically detach and return the last version, preserving concurrent edits
    // that completed before unload. Existing snapshots remain valid.
    [[nodiscard]] std::optional<ChunkSnapshot> extractChunk(ChunkCoord coordinate);
    [[nodiscard]] std::size_t chunkCount() const;
    [[nodiscard]] BlockId getBlock(BlockCoord coordinate) const;
    // Non-air writes allocate on demand. Removing from missing space does not allocate.
    bool setBlock(BlockCoord coordinate, BlockId id);
    bool compareExchangeBlock(BlockCoord coordinate, BlockId expected, BlockId desired);
    [[nodiscard]] std::optional<ChunkSnapshot> snapshot(ChunkCoord coordinate) const;
    [[nodiscard]] std::vector<ChunkSnapshot> snapshots() const;
    bool compactChunk(ChunkCoord coordinate);
    // Fully prepared replacement; default refuses to overwrite an existing chunk.
    bool insertChunk(ChunkCoord coordinate, BlockStorage data, bool replace = false);

private:
    // Lock order is always map, then chunk. Hold the map lock through an edit so
    // an unload cannot detach a chunk under a successful write. Different chunks
    // can be edited concurrently under shared map ownership.
    mutable std::shared_mutex mutex_;
    std::unordered_map<ChunkCoord, std::unique_ptr<Chunk>, ChunkCoordHash> chunks_;
};
} // namespace voxel::world
