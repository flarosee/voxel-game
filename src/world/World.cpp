#include "World.hpp"
#include <mutex>

namespace voxel::world {
bool World::allocateChunk(ChunkCoord coordinate, BlockId fill) {
    validate(coordinate);
    std::unique_lock lock(mutex_);
    if (chunks_.contains(coordinate)) return false;
    chunks_.emplace(coordinate, std::make_unique<Chunk>(fill));
    return true;
}
bool World::eraseChunk(ChunkCoord coordinate) {
    validate(coordinate);
    std::unique_lock lock(mutex_);
    return chunks_.erase(coordinate) != 0;
}
std::optional<ChunkSnapshot> World::extractChunk(ChunkCoord coordinate) {
    validate(coordinate);
    std::unique_lock lock(mutex_);
    const auto found = chunks_.find(coordinate);
    if (found == chunks_.end()) return std::nullopt;
    ChunkSnapshot result{coordinate, found->second->snapshot()};
    chunks_.erase(found);
    return result;
}
std::size_t World::chunkCount() const {
    std::shared_lock lock(mutex_);
    return chunks_.size();
}
BlockId World::getBlock(BlockCoord coordinate) const {
    const auto address = addressOf(coordinate);
    std::shared_lock lock(mutex_);
    const auto found = chunks_.find(address.chunk);
    return found == chunks_.end() ? Air : found->second->get(indexOf(address.local));
}
bool World::setBlock(BlockCoord coordinate, BlockId id) {
    const auto address = addressOf(coordinate);
    const auto index = indexOf(address.local);
    {
        std::shared_lock lock(mutex_);
        const auto found = chunks_.find(address.chunk);
        if (found != chunks_.end()) return found->second->set(index, id);
        if (id == Air) return false;
    }
    std::unique_lock lock(mutex_);
    const auto found = chunks_.find(address.chunk);
    if (found != chunks_.end()) return found->second->set(index, id);
    auto chunk = std::make_unique<Chunk>();
    chunk->set(index, id);
    chunks_.emplace(address.chunk, std::move(chunk));
    return true;
}
bool World::compareExchangeBlock(BlockCoord coordinate, BlockId expected, BlockId desired) {
    const auto address = addressOf(coordinate);
    const auto index = indexOf(address.local);
    {
        std::shared_lock lock(mutex_);
        const auto found = chunks_.find(address.chunk);
        if (found != chunks_.end()) return found->second->compareExchange(index, expected, desired);
        if (expected != Air) return false;
        if (desired == Air) return true;
    }
    std::unique_lock lock(mutex_);
    const auto found = chunks_.find(address.chunk);
    if (found != chunks_.end()) return found->second->compareExchange(index, expected, desired);
    auto chunk = std::make_unique<Chunk>();
    chunk->set(index, desired);
    chunks_.emplace(address.chunk, std::move(chunk));
    return true;
}
std::optional<ChunkSnapshot> World::snapshot(ChunkCoord coordinate) const {
    validate(coordinate);
    std::shared_lock lock(mutex_);
    const auto found = chunks_.find(coordinate);
    if (found == chunks_.end()) return std::nullopt;
    return ChunkSnapshot{coordinate, found->second->snapshot()};
}
std::vector<ChunkSnapshot> World::snapshots() const {
    std::shared_lock lock(mutex_);
    std::vector<ChunkSnapshot> result;
    result.reserve(chunks_.size());
    for (const auto& [coordinate, chunk] : chunks_) result.push_back({coordinate, chunk->snapshot()});
    return result;
}
bool World::compactChunk(ChunkCoord coordinate) {
    validate(coordinate);
    std::shared_lock lock(mutex_);
    const auto found = chunks_.find(coordinate);
    if (found == chunks_.end()) return false;
    found->second->compact();
    return true;
}
bool World::insertChunk(ChunkCoord coordinate, BlockStorage data, bool replace) {
    validate(coordinate);
    auto chunk = std::make_unique<Chunk>(std::move(data));
    std::unique_lock lock(mutex_);
    const auto found = chunks_.find(coordinate);
    if (found != chunks_.end()) {
        if (!replace) return false;
        found->second = std::move(chunk);
    } else chunks_.emplace(coordinate, std::move(chunk));
    return true;
}
} // namespace voxel::world
