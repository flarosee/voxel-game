#pragma once
#include "GreedyMesher.hpp"
#include <functional>

namespace voxel::mesh {
struct DrawRange { std::uint32_t first=0, count=0; };
struct TransparentNode {
    std::int32_t axis=0, plane=0;
    std::int32_t low=-1, high=-1;
    DrawRange faces;
};
// Axis-aligned BSP. Crossing greedy rectangles are split at integer planes;
// UVs and light values remain identical. No camera-dependent geometry upload.
struct Transparency {
    std::vector<TransparentNode> nodes;
    std::vector<DrawRange> backToFront(std::array<float,3> eye) const;
};
// Appends fragments to the scene buffer. nullopt means cancelled; caller discards
// the whole private scene. The vertex budget includes opaque/cutout geometry.
std::optional<Transparency> buildTransparency(std::vector<Quad> faces,
    std::vector<Vertex>& vertices, std::size_t maxVertices,
    const std::function<bool()>& cancelled = {});
} // namespace voxel::mesh
