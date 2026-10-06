#pragma once

#include "../Types.hpp"

#include "Coordinates.hpp"
#include <array>
#include <span>
#include <vector>

namespace voxel::world {

// Value type. Mutations are private to Chunk; published snapshots only expose const access.
class BlockStorage final {
public:
    explicit BlockStorage(BlockId fill = Air) noexcept;
    BlockStorage(const BlockStorage&) = default;
    BlockStorage(BlockStorage&& other) noexcept { swap(other); }
    BlockStorage& operator=(BlockStorage other) noexcept { swap(other); return *this; }
    [[nodiscard]] BlockId at(std::size_t index) const;
    [[nodiscard]] BlockId at(LocalCoord local) const { return at(indexOf(local)); }
    [[nodiscard]] std::uint32_t occupied() const noexcept { return occupied_; }
    [[nodiscard]] unsigned bitsPerBlock() const noexcept { return bits_; }
    [[nodiscard]] std::size_t payloadBytes() const noexcept;
    [[nodiscard]] std::size_t allocatedBytes() const noexcept;
    [[nodiscard]] static BlockStorage fromBlocks(std::span<const BlockId, ChunkVolume> blocks);

private:
    friend class Chunk;
    bool set(std::size_t index, BlockId id);
    void compact();
    void swap(BlockStorage& other) noexcept;
    [[nodiscard]] std::uint16_t raw(std::size_t index) const noexcept;
    void write(std::size_t index, std::uint16_t value) noexcept;

    // 0 = uniform; 1/2/4/8 = palette indices; 16 = direct IDs, without a palette.
    unsigned bits_ = 0;
    BlockId uniform_ = Air;
    std::uint32_t occupied_ = 0;
    std::vector<BlockId> palette_;
    std::vector<UInt64> words_;
};

} // namespace voxel::world
