#include "Portability.hpp"
#include "mesh/GreedyMesher.hpp"
#include "mesh/Transparency.hpp"
#include "world/ChunkCodec.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <type_traits>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
}

int main() {
    using namespace voxel;
    static_assert(std::is_same_v<world::BlockId, std::uint16_t>);
    static_assert(std::is_same_v<decltype(world::BlockCoord::x), std::int64_t>);
    static_assert(std::is_same_v<decltype(world::ChunkCoord::x), std::int64_t>);
    static_assert(std::is_same_v<decltype(world::LocalCoord::x), std::int32_t>);
    static_assert(std::is_same_v<decltype(mesh::Vertex::material), std::uint32_t>);
    static_assert(std::is_same_v<decltype(mesh::DrawRange::first), std::uint32_t>);

    world::Chunk chunk(0x1234);
    const auto bytes = world::encodeChunk({
        {INT64_C(0x0102030405060708), -INT64_C(0x0102030405060708), 0},
        chunk.snapshot()});
    const std::array<std::uint8_t, 8> positive{8, 7, 6, 5, 4, 3, 2, 1};
    const std::array<std::uint8_t, 8> negative{248, 248, 249, 250, 251, 252, 253, 254};
    require(std::equal(positive.begin(), positive.end(), bytes.begin() + 8),
            "Positive int64 was not encoded little-endian");
    require(std::equal(negative.begin(), negative.end(), bytes.begin() + 16),
            "Negative int64 was not encoded as little-endian two's complement");
    const auto decoded = world::decodeChunk(bytes);
    require(decoded.coordinate == world::ChunkCoord{
                INT64_C(0x0102030405060708), -INT64_C(0x0102030405060708), 0},
            "Fixed-width coordinate did not round-trip");
    require(decoded.blocks.at(0) == 0x1234, "Fixed-width block ID did not round-trip");

    std::cout << "Portability contract passed\n";
}
