#pragma once

#include <climits>
#include <cstdint>
#include <limits>

namespace voxel::portability {

// VXC1 and the renderer interfaces are defined in octets and fixed-width integers.
// Fail at configure/build time on an implementation that cannot provide them.
static_assert(CHAR_BIT == 8, "VoxelGame requires 8-bit bytes");
static_assert(sizeof(std::uint8_t) == 1);
static_assert(sizeof(std::uint16_t) == 2);
static_assert(sizeof(std::uint32_t) == 4);
static_assert(sizeof(std::uint64_t) == 8);
static_assert(sizeof(std::int32_t) == 4);
static_assert(sizeof(std::int64_t) == 8);
static_assert(std::numeric_limits<std::uint8_t>::digits == 8);
static_assert(std::numeric_limits<std::uint16_t>::digits == 16);
static_assert(std::numeric_limits<std::uint32_t>::digits == 32);
static_assert(std::numeric_limits<std::uint64_t>::digits == 64);

} // namespace voxel::portability
