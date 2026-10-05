#pragma once

#include "Chunk.hpp"
#include <span>
#include <vector>

namespace voxel::world {
struct DecodedChunk { ChunkCoord coordinate; BlockStorage blocks; };

// VXC1: bounded little-endian records, never native struct dumps. All functions
// own their output and hold no world/chunk locks during compression or I/O.
inline constexpr std::size_t MaxChunkFileBytes = 40 + ChunkVolume * 4;
[[nodiscard]] std::vector<std::uint8_t> encodeChunk(const ChunkSnapshot& chunk);
[[nodiscard]] DecodedChunk decodeChunk(std::span<const std::uint8_t> bytes);
} // namespace voxel::world
