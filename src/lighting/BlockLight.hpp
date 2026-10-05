#pragma once
#include "world/Chunk.hpp"
#include <array>
#include <functional>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace voxel::lighting {
// Published fields are immutable. Updates return a new field transactionally;
// cancelled updates leave this field and its owning snapshots untouched.
class BlockLight final {
public:
    static std::optional<BlockLight> build(std::span<const world::ChunkSnapshot> snapshots,
                                          const std::function<bool()>& cancelled = {});
    std::optional<BlockLight> updated(std::span<const world::ChunkSnapshot> snapshots,
                                     const std::function<bool()>& cancelled = {}) const;
    struct UpdateStats { std::size_t scannedCells=0, processedCells=0, increases=0, decreases=0; };
    [[nodiscard]] UpdateStats updateStats() const noexcept { return stats_; }
    [[nodiscard]] std::uint8_t at(world::BlockCoord block) const;
    [[nodiscard]] std::uint8_t face(world::BlockCoord block,int axis,int sign,world::BlockId material) const;
private:
    struct Tile {
        std::array<std::uint8_t,world::ChunkVolume/2> packed{};
        std::uint8_t get(std::size_t cell) const noexcept;
        void set(std::size_t cell,std::uint8_t level) noexcept;
    };
    std::vector<Tile> tiles_;
    std::unordered_map<world::ChunkCoord,std::size_t,world::ChunkCoordHash> lookup_;
    std::vector<world::ChunkSnapshot> inputs_; // Shared owners prevent identity reuse.
    UpdateStats stats_;
};
} // namespace voxel::lighting
