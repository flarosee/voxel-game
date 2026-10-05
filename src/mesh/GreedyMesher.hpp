#pragma once

#include "world/Chunk.hpp"
#include "lighting/Sunlight.hpp"
#include "lighting/BlockLight.hpp"
#include <array>
#include <span>
#include <vector>

namespace voxel::mesh {

struct Vertex {
    std::array<float,3> position;
    std::array<float,3> normal;
    std::array<float,2> uv;
    std::uint32_t material;
    std::uint32_t sunlight = lighting::FullSunlight;
    std::uint32_t blockLight = 0;
    bool operator==(const Vertex&) const = default;
};
struct Quad {
    std::array<std::int32_t,3> origin;
    std::int32_t axis;
    std::int32_t sign;
    std::int32_t width;
    std::int32_t height;
    world::BlockId material;
    std::uint8_t sunlight = lighting::FullSunlight;
    std::uint8_t blockLight = 0;
    bool operator==(const Quad&) const = default;
};
struct ChunkMesh {
    world::ChunkCoord coordinate;
    std::vector<Quad> quads;
    std::vector<Vertex> vertices; // Triangle list, outward CCW. Positions are chunk-local.
};

// Pure, reentrant mesher. All faces in a scene use the SAME immutable snapshot
// collection. Material rules decide interfaces; missing neighbors = air.
[[nodiscard]] std::vector<ChunkMesh> build(std::span<const world::ChunkSnapshot> chunks,
                                         const lighting::Sunlight* sunlight = nullptr,
                                         const lighting::BlockLight* blockLight = nullptr);
// Mesh one target using the supplied immutable neighbor halo.
[[nodiscard]] ChunkMesh buildOne(std::span<const world::ChunkSnapshot> chunks, world::ChunkCoord target,
                               const lighting::Sunlight* sunlight = nullptr,
                               const lighting::BlockLight* blockLight = nullptr);

// Conservative rebuild dependency: any edit, replacement, compaction, load, or
// unload changes the scene. Shared owners retained by the caller prevent pointer ABA.
[[nodiscard]] bool sameSnapshots(std::span<const world::ChunkSnapshot> a,
                                 std::span<const world::ChunkSnapshot> b) noexcept;
// Also used for axis-aligned transparent fragments after spatial partitioning.
void appendQuad(std::vector<Vertex>& vertices, const Quad& quad);
} // namespace voxel::mesh
