#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <initializer_list>
#include <stdexcept>

namespace voxel::world {

using BlockId = std::uint16_t;
inline constexpr BlockId Air = 0;
inline constexpr int ChunkSide = 16;
inline constexpr std::size_t ChunkVolume = ChunkSide * ChunkSide * ChunkSide;

struct BlockCoord {
    std::int64_t x = 0, y = 0, z = 0;
    bool operator==(const BlockCoord&) const = default;
};
struct ChunkCoord {
    std::int64_t x = 0, y = 0, z = 0;
    bool operator==(const ChunkCoord&) const = default;
};
struct LocalCoord { int x = 0, y = 0, z = 0; };
struct BlockAddress { ChunkCoord chunk; LocalCoord local; };

inline constexpr std::int64_t MinChunkCoord = std::numeric_limits<std::int64_t>::min() / ChunkSide;
inline constexpr std::int64_t MaxChunkCoord = std::numeric_limits<std::int64_t>::max() / ChunkSide;

inline void validate(ChunkCoord coordinate) {
    for (auto value : {coordinate.x, coordinate.y, coordinate.z}) {
        if (value < MinChunkCoord || value > MaxChunkCoord)
            throw std::out_of_range("Chunk coordinate exceeds signed 64-bit block space");
    }
}

inline BlockAddress addressOf(BlockCoord block) noexcept {
    const auto divide = [](std::int64_t value) {
        const auto quotient = value / ChunkSide;
        return quotient - (value % ChunkSide < 0 ? 1 : 0);
    };
    const auto local = [](std::int64_t value) {
        const auto remainder = value % ChunkSide;
        return static_cast<int>(remainder < 0 ? remainder + ChunkSide : remainder);
    };
    return {{divide(block.x), divide(block.y), divide(block.z)},
            {local(block.x), local(block.y), local(block.z)}};
}

inline std::size_t indexOf(LocalCoord local) {
    if (local.x < 0 || local.x >= ChunkSide || local.y < 0 || local.y >= ChunkSide ||
        local.z < 0 || local.z >= ChunkSide) throw std::out_of_range("Invalid local block coordinate");
    return static_cast<std::size_t>(local.x + ChunkSide * (local.y + ChunkSide * local.z));
}

inline LocalCoord localOf(std::size_t index) {
    if (index >= ChunkVolume) throw std::out_of_range("Invalid block index");
    return {static_cast<int>(index % ChunkSide), static_cast<int>((index / ChunkSide) % ChunkSide),
            static_cast<int>(index / (ChunkSide * ChunkSide))};
}

inline BlockCoord blockAt(ChunkCoord chunk, LocalCoord local) {
    validate(chunk);
    (void)indexOf(local);
    // Validated limits guarantee multiply/add cannot overflow, including INT64_MIN/MAX.
    return {chunk.x * ChunkSide + local.x, chunk.y * ChunkSide + local.y, chunk.z * ChunkSide + local.z};
}

struct ChunkCoordHash {
    std::size_t operator()(ChunkCoord value) const noexcept {
        const auto mix = [](std::uint64_t x) {
            x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
            x = (x ^ (x >> 27)) * UINT64_C(0x94d049bb133111eb);
            return x ^ (x >> 31);
        };
        return static_cast<std::size_t>(mix(static_cast<std::uint64_t>(value.x)) ^
            mix(static_cast<std::uint64_t>(value.y) + UINT64_C(0x9e3779b97f4a7c15)) ^
            mix(static_cast<std::uint64_t>(value.z) + UINT64_C(0x3c6ef372fe94f82a)));
    }
};

} // namespace voxel::world
