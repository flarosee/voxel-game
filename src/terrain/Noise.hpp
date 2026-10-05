#pragma once

#include <cstdint>

namespace voxel::terrain {

// Seeded smooth value noise. Integer coordinates + Q16 interpolation retain exact
// lattice addressing at INT64_MIN/MAX and produce platform-independent results.
class Noise final {
public:
    explicit Noise(std::uint64_t seed) noexcept : seed_(seed) {}
    // Output [-32768,32767]. Cell side is 2^cellShift blocks; valid shifts [1,30].
    [[nodiscard]] std::int32_t sample2D(std::int64_t x, std::int64_t z,
                                      unsigned cellShift, std::uint64_t channel = 0) const;
private:
    [[nodiscard]] std::int32_t lattice(std::int64_t x, std::int64_t z, std::uint64_t channel) const noexcept;
    const std::uint64_t seed_;
};
} // namespace voxel::terrain
