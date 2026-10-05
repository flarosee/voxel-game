#include "Chunk.hpp"
#include <mutex>
#include <utility>

namespace voxel::world {
Chunk::Chunk(BlockId fill) : data_(std::make_shared<BlockStorage>(fill)) {}
Chunk::Chunk(BlockStorage data) : data_(std::make_shared<BlockStorage>(std::move(data))) {}
BlockId Chunk::get(std::size_t index) const {
    std::shared_lock lock(mutex_);
    return data_->at(index);
}
std::shared_ptr<const BlockStorage> Chunk::snapshot() const {
    std::unique_lock lock(mutex_);
    published_ = true;
    return data_;
}
void Chunk::setLocked(std::size_t index, BlockId id) {
    if (!published_) data_->set(index, id);
    else {
        auto next = std::make_shared<BlockStorage>(*data_);
        next->set(index, id);
        data_ = std::move(next);
        published_ = false;
    }
}
bool Chunk::set(std::size_t index, BlockId id) {
    std::unique_lock lock(mutex_);
    if (data_->at(index) == id) return false;
    setLocked(index, id);
    return true;
}
bool Chunk::compareExchange(std::size_t index, BlockId expected, BlockId desired) {
    std::unique_lock lock(mutex_);
    if (data_->at(index) != expected) return false;
    if (expected != desired) setLocked(index, desired);
    return true;
}
void Chunk::compact() {
    std::unique_lock lock(mutex_);
    if (!published_) data_->compact();
    else {
        auto next = std::make_shared<BlockStorage>(*data_);
        next->compact();
        data_ = std::move(next);
        published_ = false;
    }
}
} // namespace voxel::world
