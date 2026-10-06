#include "../Types.hpp"
#include "Noise.hpp"
#include <stdexcept>

namespace voxel::terrain {
namespace {
UInt64 mix(UInt64 value) noexcept {
    value = (value ^ (value >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    value = (value ^ (value >> 27)) * UINT64_C(0x94d049bb133111eb);
    return value ^ (value >> 31);
}
struct Axis { std::int64_t cell; std::int64_t fraction; };
Axis split(std::int64_t coordinate, unsigned shift) noexcept {
    const std::int64_t side = INT64_C(1) << shift;
    auto cell = coordinate / side;
    auto remainder = coordinate % side;
    if (remainder < 0) { --cell; remainder += side; }
    return {cell, remainder * 65536 / side};
}
std::int64_t smooth(std::int64_t t) noexcept {
    return t * t * (3 * 65536 - 2 * t) / INT64_C(4294967296);
}
std::int32_t lerp(std::int32_t a, std::int32_t b, std::int64_t t) noexcept {
    return static_cast<std::int32_t>(a + (static_cast<std::int64_t>(b) - a) * t / 65536);
}
}
std::int32_t Noise::lattice(std::int64_t x, std::int64_t z, UInt64 channel) const noexcept {
    const auto hash = mix(mix(seed_ ^ UINT64_C(0x6a09e667f3bcc909)) ^
        mix(static_cast<UInt64>(x) + UINT64_C(0x9e3779b97f4a7c15)) ^
        mix(static_cast<UInt64>(z) + UINT64_C(0x3c6ef372fe94f82a)) ^ mix(channel));
    return static_cast<std::int32_t>(hash & 65535U) - 32768;
}
std::int32_t Noise::sample2D(std::int64_t x, std::int64_t z, unsigned shift, UInt64 channel) const {
    if (shift < 1 || shift > 30) throw std::out_of_range("Noise cell shift must be within [1,30]");
    const auto u = split(x, shift);
    const auto v = split(z, shift);
    // shift >= 1 leaves room for the next lattice cell even at coordinate limits.
    return lerp(lerp(lattice(u.cell,v.cell,channel), lattice(u.cell+1,v.cell,channel), smooth(u.fraction)),
                lerp(lattice(u.cell,v.cell+1,channel), lattice(u.cell+1,v.cell+1,channel), smooth(u.fraction)),
                smooth(v.fraction));
}
} // namespace voxel::terrain
