#include "GreedyMesher.hpp"
#include "world/BlockTypes.hpp"
#include <algorithm>
#include <unordered_map>

namespace voxel::mesh {
namespace {
using Lookup = std::unordered_map<world::ChunkCoord, const world::BlockStorage*, world::ChunkCoordHash>;
constexpr int Side = world::ChunkSide;

world::BlockId neighbor(const Lookup& lookup, const world::ChunkSnapshot& chunk,
                        std::array<int,3> local, int axis, int sign) {
    local[axis] += sign;
    if (local[axis] >= 0 && local[axis] < Side)
        return chunk.blocks->at({local[0],local[1],local[2]});
    auto coordinate = chunk.coordinate;
    auto& component = axis == 0 ? coordinate.x : axis == 1 ? coordinate.y : coordinate.z;
    if ((sign < 0 && component == world::MinChunkCoord) ||
        (sign > 0 && component == world::MaxChunkCoord)) return world::Air;
    component += sign;
    const auto found = lookup.find(coordinate);
    if (found == lookup.end()) return world::Air;
    local[axis] = sign > 0 ? 0 : Side-1;
    return found->second->at({local[0],local[1],local[2]});
}

Vertex vertex(const Quad& quad, float u, float v) {
    const auto uAxis = (quad.axis+1)%3;
    const auto vAxis = (quad.axis+2)%3;
    Vertex result{};
    result.position[quad.axis] = static_cast<float>(quad.origin[quad.axis]);
    result.position[uAxis] = u;
    result.position[vAxis] = v;
    result.normal[quad.axis] = static_cast<float>(quad.sign);
    // One UV unit per voxel, including merged faces. Sign makes U x V point out.
    // Chunk origins are multiples of 16, so integer-repeat phase matches at seams.
    result.uv = {u * static_cast<float>(quad.sign), v};
    result.material = quad.material;
    result.sunlight = quad.sunlight;
    result.blockLight = quad.blockLight;
    return result;
}

void triangulate(std::vector<Vertex>& vertices, const Quad& quad) {
    const int u = quad.origin[(quad.axis+1)%3], v = quad.origin[(quad.axis+2)%3];
    std::vector<Vertex> boundary;
    // Unit edge segments remove T-junctions without depending on neighboring
    // rectangles' greedy partition. Deliberately favor conformity over triangle count.
    for (int i=0; i<quad.width; ++i) boundary.push_back(vertex(quad,static_cast<float>(u+i),static_cast<float>(v)));
    for (int i=0; i<quad.height; ++i) boundary.push_back(vertex(quad,static_cast<float>(u+quad.width),static_cast<float>(v+i)));
    for (int i=0; i<quad.width; ++i) boundary.push_back(vertex(quad,static_cast<float>(u+quad.width-i),static_cast<float>(v+quad.height)));
    for (int i=0; i<quad.height; ++i) boundary.push_back(vertex(quad,static_cast<float>(u),static_cast<float>(v+quad.height-i)));
    const auto center = vertex(quad,static_cast<float>(u)+static_cast<float>(quad.width)*0.5F,
                              static_cast<float>(v)+static_cast<float>(quad.height)*0.5F);
    for (std::size_t i=0; i<boundary.size(); ++i) {
        vertices.push_back(center);
        vertices.push_back(boundary[quad.sign > 0 ? i : (i+1)%boundary.size()]);
        vertices.push_back(boundary[quad.sign > 0 ? (i+1)%boundary.size() : i]);
    }
}

ChunkMesh buildChunk(const Lookup& lookup, const world::ChunkSnapshot& chunk, const lighting::Sunlight* sunlight,
                     const lighting::BlockLight* blockLight) {
    ChunkMesh result{chunk.coordinate,{}, {}};
    if (chunk.blocks->occupied() == 0) return result;
    for (int axis=0; axis<3; ++axis) for (int sign : {-1,1}) for (int slice=0; slice<Side; ++slice) {
        const int uAxis = (axis+1)%3, vAxis = (axis+2)%3;
        // Material and light are both part of the merge key. A shadow boundary
        // must split a rectangle instead of interpolating sunlight through it.
        std::array<std::uint32_t,Side*Side> mask{};
        for (int v=0; v<Side; ++v) for (int u=0; u<Side; ++u) {
            std::array<int,3> local{};
            local[axis]=slice; local[uAxis]=u; local[vAxis]=v;
            const auto id = chunk.blocks->at({local[0],local[1],local[2]});
            if (world::blocks::faceVisible(id,neighbor(lookup,chunk,local,axis,sign)))
                mask[static_cast<std::size_t>(v*Side+u)] = static_cast<std::uint32_t>(id) |
                    (static_cast<std::uint32_t>(sunlight ? sunlight->face(world::blockAt(chunk.coordinate,
                        {local[0],local[1],local[2]}),axis,sign,id) : lighting::FullSunlight) << 16) |
                    (static_cast<std::uint32_t>(blockLight ? blockLight->face(world::blockAt(chunk.coordinate,
                        {local[0],local[1],local[2]}),axis,sign,id) : 0) << 20);
        }
        for (int v=0; v<Side; ++v) for (int u=0; u<Side;) {
            const auto id = mask[static_cast<std::size_t>(v*Side+u)];
            if (id == world::Air) { ++u; continue; }
            int width=1;
            while (u+width<Side && mask[static_cast<std::size_t>(v*Side+u+width)] == id) ++width;
            int height=1;
            while (v+height<Side) {
                bool matches=true;
                for (int x=0; x<width; ++x)
                    if (mask[static_cast<std::size_t>((v+height)*Side+u+x)] != id) { matches=false; break; }
                if (!matches) break;
                ++height;
            }
            Quad quad{{},axis,sign,width,height,static_cast<world::BlockId>(id & 65535U),
                      static_cast<std::uint8_t>((id >> 16) & 15U),static_cast<std::uint8_t>((id >> 20) & 15U)};
            quad.origin[axis]=slice+(sign>0 ? 1 : 0);
            quad.origin[uAxis]=u; quad.origin[vAxis]=v;
            result.quads.push_back(quad);
            triangulate(result.vertices,quad);
            for (int y=0; y<height; ++y) for (int x=0; x<width; ++x)
                mask[static_cast<std::size_t>((v+y)*Side+u+x)] = world::Air;
            u += width;
        }
    }
    return result;
}
}

void appendQuad(std::vector<Vertex>& vertices,const Quad& quad) { triangulate(vertices,quad); }

std::vector<ChunkMesh> build(std::span<const world::ChunkSnapshot> chunks, const lighting::Sunlight* sunlight,
                           const lighting::BlockLight* blockLight) {
    Lookup lookup;
    for (const auto& chunk : chunks) {
        world::validate(chunk.coordinate);
        if (!chunk.blocks) throw std::invalid_argument("Meshing requires non-null snapshots");
        if (!lookup.emplace(chunk.coordinate,chunk.blocks.get()).second)
            throw std::invalid_argument("Duplicate coordinate in mesh snapshot set");
    }
    std::vector<ChunkMesh> result;
    for (const auto& chunk : chunks) result.push_back(buildChunk(lookup,chunk,sunlight,blockLight));
    return result;
}
ChunkMesh buildOne(std::span<const world::ChunkSnapshot> chunks, world::ChunkCoord target, const lighting::Sunlight* sunlight,
                   const lighting::BlockLight* blockLight) {
    Lookup lookup;
    const world::ChunkSnapshot* selected = nullptr;
    for (const auto& chunk : chunks) {
        world::validate(chunk.coordinate);
        if (!chunk.blocks || !lookup.emplace(chunk.coordinate, chunk.blocks.get()).second)
            throw std::invalid_argument("Invalid meshing snapshot set");
        if (chunk.coordinate == target) selected = &chunk;
    }
    if (!selected) throw std::invalid_argument("Missing mesh target");
    return buildChunk(lookup, *selected,sunlight,blockLight);
}
bool sameSnapshots(std::span<const world::ChunkSnapshot> a, std::span<const world::ChunkSnapshot> b) noexcept {
    if (a.size() != b.size()) return false;
    for (const auto& chunk : a) {
        const auto found = std::find_if(b.begin(),b.end(),[&](const auto& other) { return chunk.coordinate == other.coordinate; });
        if (found == b.end() || found->blocks != chunk.blocks) return false;
    }
    return true;
}
} // namespace voxel::mesh
