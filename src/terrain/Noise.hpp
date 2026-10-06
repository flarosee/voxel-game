#pragma once

#include "../Types.hpp"

#include <cstdint>

namespace voxel::terrain {

// Seeded smooth value noise. Integer coordinates + Q16 interpolation retain exact
// lattice addressing at INT64_MIN/MAX and produce platform-independent results.
class Noise final {
public:
    explicit Noise(UInt64 seed) noexcept : seed_(seed) {}
    // Output [-32768,32767]. Cell side is 2^cellShift blocks; valid shifts [1,30].
    [[nodiscard]] std::int32_t sample2D(std::int64_t x, std::int64_t z,
                                      unsigned cellShift, UInt64 channel = 0) const;
private:
    [[nodiscard]] std::int32_t lattice(std::int64_t x, std::int64_t z, UInt64 channel) const noexcept;
    const UInt64 seed_;
};
} // namespace voxel::terrain
