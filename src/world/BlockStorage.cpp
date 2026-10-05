#include "BlockStorage.hpp"
#include <algorithm>
#include <utility>

namespace voxel::world {
namespace {
unsigned widthFor(std::size_t size) {
    if (size <= 1) return 0;
    if (size <= 2) return 1;
    if (size <= 4) return 2;
    if (size <= 16) return 4;
    if (size <= 256) return 8;
    return 16;
}
}

BlockStorage::BlockStorage(BlockId fill) noexcept
    : uniform_(fill), occupied_(fill == Air ? 0U : static_cast<std::uint32_t>(ChunkVolume)) {}

std::uint16_t BlockStorage::raw(std::size_t index) const noexcept {
    const auto bit = index * bits_;
    return static_cast<std::uint16_t>((words_[bit / 64] >> (bit % 64)) & ((UINT64_C(1) << bits_) - 1));
}

void BlockStorage::write(std::size_t index, std::uint16_t value) noexcept {
    const auto bit = index * bits_;
    const auto shift = bit % 64;
    const auto mask = ((UINT64_C(1) << bits_) - 1) << shift;
    words_[bit / 64] = (words_[bit / 64] & ~mask) | (static_cast<std::uint64_t>(value) << shift);
}

BlockId BlockStorage::at(std::size_t index) const {
    if (index >= ChunkVolume) throw std::out_of_range("Invalid block index");
    if (bits_ == 0) return uniform_;
    const auto value = raw(index);
    return bits_ == 16 ? value : palette_[value];
}

std::size_t BlockStorage::payloadBytes() const noexcept {
    return palette_.size() * sizeof(BlockId) + words_.size() * sizeof(std::uint64_t);
}
std::size_t BlockStorage::allocatedBytes() const noexcept {
    return palette_.capacity() * sizeof(BlockId) + words_.capacity() * sizeof(std::uint64_t);
}
void BlockStorage::swap(BlockStorage& other) noexcept {
    std::swap(bits_, other.bits_);
    std::swap(uniform_, other.uniform_);
    std::swap(occupied_, other.occupied_);
    palette_.swap(other.palette_);
    words_.swap(other.words_);
}

BlockStorage BlockStorage::fromBlocks(std::span<const BlockId, ChunkVolume> blocks) {
    BlockStorage result;
    for (BlockId id : blocks) {
        if (id != Air) ++result.occupied_;
        if (result.palette_.size() <= 256 &&
            std::find(result.palette_.begin(), result.palette_.end(), id) == result.palette_.end())
            result.palette_.push_back(id);
    }
    result.bits_ = widthFor(result.palette_.size());
    if (result.bits_ == 0) return BlockStorage(blocks[0]);
    if (result.bits_ == 16) std::vector<BlockId>().swap(result.palette_);
    else {
        // Bounded capacity, even if vector growth overshot the last palette tier.
        std::vector<BlockId>(result.palette_).swap(result.palette_);
    }
    result.words_.resize(ChunkVolume * result.bits_ / 64, 0);
    for (std::size_t i = 0; i < ChunkVolume; ++i) {
        const auto value = result.bits_ == 16 ? blocks[i] : static_cast<BlockId>(
            std::find(result.palette_.begin(), result.palette_.end(), blocks[i]) - result.palette_.begin());
        result.write(i, value);
    }
    return result;
}

bool BlockStorage::set(std::size_t index, BlockId id) {
    const auto previous = at(index);
    if (previous == id) return false;
    const auto occupied = occupied_ - (previous != Air ? 1U : 0U) + (id != Air ? 1U : 0U);
    if (occupied == 0) {
        BlockStorage empty;
        swap(empty);
        return true;
    }
    if (bits_ == 16) {
        write(index, id);
    } else if (bits_ == 0) {
        // Allocate entirely before committing: bad_alloc never partially edits a chunk.
        BlockStorage next;
        next.bits_ = 1;
        next.palette_ = {uniform_, id};
        next.words_.resize(ChunkVolume / 64, 0);
        next.write(index, 1);
        swap(next);
    } else {
        const auto found = std::find(palette_.begin(), palette_.end(), id);
        if (found != palette_.end()) {
            write(index, static_cast<BlockId>(found - palette_.begin()));
        } else if (palette_.size() == 256) {
            // Reclaim unused entries before falling back to direct IDs.
            std::array<BlockId, ChunkVolume> blocks{};
            for (std::size_t i = 0; i < ChunkVolume; ++i) blocks[i] = at(i);
            blocks[index] = id;
            auto next = fromBlocks(blocks);
            swap(next);
        } else {
            auto palette = palette_;
            palette.push_back(id);
            const auto width = widthFor(palette.size());
            if (width != bits_) {
                BlockStorage next;
                next.bits_ = width;
                next.palette_ = std::move(palette);
                next.words_.resize(ChunkVolume * width / 64, 0);
                for (std::size_t i = 0; i < ChunkVolume; ++i) next.write(i, raw(i));
                next.write(index, static_cast<BlockId>(next.palette_.size() - 1));
                swap(next);
            } else {
                palette_.swap(palette);
                write(index, static_cast<BlockId>(palette_.size() - 1));
            }
        }
    }
    occupied_ = occupied;
    return true;
}

void BlockStorage::compact() {
    if (bits_ == 0) return;
    std::array<BlockId, ChunkVolume> blocks{};
    for (std::size_t i = 0; i < ChunkVolume; ++i) blocks[i] = at(i);
    auto next = fromBlocks(blocks);
    swap(next);
}
} // namespace voxel::world
