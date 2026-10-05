#pragma once

#include "BlockStorage.hpp"
#include <memory>
#include <shared_mutex>

namespace voxel::world {

struct ChunkSnapshot {
    ChunkCoord coordinate;
    std::shared_ptr<const BlockStorage> blocks;
};

// World owns Chunk lifetime. No live references to its mutable storage escape.
class Chunk final {
public:
    explicit Chunk(BlockId fill = Air);
    explicit Chunk(BlockStorage data);
    [[nodiscard]] BlockId get(std::size_t index) const;
    [[nodiscard]] std::shared_ptr<const BlockStorage> snapshot() const;
    bool set(std::size_t index, BlockId id);
    bool compareExchange(std::size_t index, BlockId expected, BlockId desired);
    void compact();

private:
    void setLocked(std::size_t index, BlockId id);
    mutable std::shared_mutex mutex_;
    // Never infer mutability from shared_ptr::use_count(): weak_ptr::lock() can
    // resurrect an old snapshot concurrently. Once published, storage stays immutable.
    mutable bool published_ = false;
    std::shared_ptr<BlockStorage> data_;
};
} // namespace voxel::world
