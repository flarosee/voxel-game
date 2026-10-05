#include "ChunkCodec.hpp"
#include <array>
#include <bit>
#include <limits>

namespace voxel::world {
namespace {
void append(std::vector<std::uint8_t>& bytes, std::uint64_t value, unsigned count) {
    for (unsigned i = 0; i < count; ++i) bytes.push_back(static_cast<std::uint8_t>(value >> (i * 8)));
}
std::uint64_t read(std::span<const std::uint8_t> bytes, std::size_t& offset, unsigned count) {
    if (offset > bytes.size() || count > bytes.size() - offset) throw std::runtime_error("Truncated chunk");
    std::uint64_t value = 0;
    for (unsigned i = 0; i < count; ++i) value |= static_cast<std::uint64_t>(bytes[offset++]) << (i * 8);
    return value;
}
std::uint32_t crc32(std::span<const std::uint8_t> bytes) noexcept {
    std::uint32_t crc = UINT32_MAX;
    for (auto byte : bytes) {
        crc ^= byte;
        for (unsigned bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ ((crc & 1U) ? 0xedb88320U : 0U);
    }
    return ~crc;
}
}

std::vector<std::uint8_t> encodeChunk(const ChunkSnapshot& chunk) {
    validate(chunk.coordinate);
    if (!chunk.blocks) throw std::invalid_argument("Cannot encode a null snapshot");
    std::vector<std::uint8_t> bytes{'V','X','C','1'};
    append(bytes, 1, 2); // Version.
    append(bytes, ChunkSide, 2);
    for (auto value : {chunk.coordinate.x, chunk.coordinate.y, chunk.coordinate.z})
        append(bytes, std::bit_cast<std::uint64_t>(value), 8);
    append(bytes, 0, 4); // Payload byte count, patched below.
    for (std::size_t index = 0; index < ChunkVolume;) {
        const auto id = chunk.blocks->at(index);
        std::size_t end = index + 1;
        while (end < ChunkVolume && chunk.blocks->at(end) == id) ++end;
        append(bytes, id, 2);
        append(bytes, end - index, 2);
        index = end;
    }
    const auto payload = bytes.size() - 36;
    for (unsigned i = 0; i < 4; ++i) bytes[32 + i] = static_cast<std::uint8_t>(payload >> (i * 8));
    append(bytes, crc32(bytes), 4);
    return bytes;
}

DecodedChunk decodeChunk(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < 44 || bytes.size() > MaxChunkFileBytes) throw std::runtime_error("Invalid chunk file size");
    if (bytes[0] != 'V' || bytes[1] != 'X' || bytes[2] != 'C' || bytes[3] != '1')
        throw std::runtime_error("Invalid chunk magic");
    std::size_t offset = bytes.size() - 4;
    if (read(bytes, offset, 4) != crc32(bytes.first(bytes.size() - 4))) throw std::runtime_error("Chunk checksum mismatch");
    offset = 4;
    if (read(bytes, offset, 2) != 1) throw std::runtime_error("Unsupported chunk version");
    if (read(bytes, offset, 2) != ChunkSide) throw std::runtime_error("Unsupported chunk dimensions");
    ChunkCoord coordinate;
    coordinate.x = std::bit_cast<std::int64_t>(read(bytes, offset, 8));
    coordinate.y = std::bit_cast<std::int64_t>(read(bytes, offset, 8));
    coordinate.z = std::bit_cast<std::int64_t>(read(bytes, offset, 8));
    validate(coordinate);
    const auto payload = read(bytes, offset, 4);
    if (payload == 0 || payload % 4 != 0 || payload != bytes.size() - 40)
        throw std::runtime_error("Invalid chunk payload length");
    std::array<BlockId, ChunkVolume> blocks{};
    std::size_t written = 0;
    BlockId previous = 0;
    while (offset < bytes.size() - 4) {
        const auto id = static_cast<BlockId>(read(bytes, offset, 2));
        const auto length = read(bytes, offset, 2);
        if (length == 0 || length > ChunkVolume - written) throw std::runtime_error("Invalid chunk run length");
        if (written != 0 && id == previous) throw std::runtime_error("Noncanonical adjacent chunk runs");
        for (std::uint64_t i = 0; i < length; ++i) blocks[written++] = id;
        previous = id;
    }
    if (written != ChunkVolume) throw std::runtime_error("Incomplete chunk payload");
    return {coordinate, BlockStorage::fromBlocks(blocks)};
}
} // namespace voxel::world
