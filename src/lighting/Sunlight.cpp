#include "Sunlight.hpp"
#include "world/BlockTypes.hpp"
#include <limits>

namespace voxel::lighting {
namespace {
world::ChunkCoord horizontal(world::ChunkCoord c) { return {c.x,0,c.z}; }
std::size_t index(int x,int z) { return static_cast<std::size_t>(x+world::ChunkSide*z); }
}
bool Sunlight::contains(world::ChunkCoord c) const { return tops_.contains(horizontal(c)); }
void Sunlight::invalidate(world::BlockCoord block) {
    if (contains(world::addressOf(block).chunk)) invalid_.insert({block.x,0,block.z});
}
void Sunlight::invalidateChunk(world::ChunkCoord coordinate) {
    const auto base=world::blockAt(coordinate,{});
    if (!contains(coordinate)) return;
    for (int z=0;z<16;++z) for (int x=0;x<16;++x) invalidate({base.x+x,base.y,base.z+z});
}
void Sunlight::add(const world::ChunkSnapshot& snapshot) {
    world::validate(snapshot.coordinate);
    if (!snapshot.blocks) throw std::invalid_argument("Null sunlight snapshot");
    tops_.try_emplace(horizontal(snapshot.coordinate));
    include(snapshot);
}
void Sunlight::include(const world::ChunkSnapshot& snapshot) {
    world::validate(snapshot.coordinate);
    if (!snapshot.blocks) throw std::invalid_argument("Null sunlight snapshot");
    const auto found=tops_.find(horizontal(snapshot.coordinate));
    if (found==tops_.end() || snapshot.blocks->occupied()==0) return;
    const auto base=world::blockAt(snapshot.coordinate,{});
    for (int z=0;z<world::ChunkSide;++z) for (int x=0;x<world::ChunkSide;++x) {
        for (int y=world::ChunkSide-1;y>=0;--y) if (world::blocks::opaque(snapshot.blocks->at({x,y,z}))) {
            auto& top=found->second[index(x,z)];
            const auto height=base.y+y;
            if (!top || height>*top) top=height;
            break;
        }
    }
}
std::optional<Sunlight> Sunlight::terrain(const terrain::TerrainGenerator& generator,
    std::span<const world::ChunkSnapshot> snapshots, const OverrideReader& read, const Cancelled& cancelled,
    const Sunlight* previous) {
    Sunlight result;
    for (const auto& snapshot:snapshots) {
        world::validate(snapshot.coordinate);
        if (!snapshot.blocks) throw std::invalid_argument("Null sunlight snapshot");
        result.tops_.try_emplace(horizontal(snapshot.coordinate));
    }
    for (auto& [coordinate,tile]:result.tops_) {
        const auto base=world::blockAt(coordinate,{});
        for (int z=0;z<world::ChunkSide;++z) for (int x=0;x<world::ChunkSide;++x) {
            if (cancelled()) return std::nullopt;
            if (previous && !previous->invalid_.contains({base.x+x,0,base.z+z})) {
                const auto cached=previous->tops_.find(coordinate);
                if (cached!=previous->tops_.end()) { tile[index(x,z)]=cached->second[index(x,z)]; continue; }
            }
            ++result.recomputed_;
            auto height=generator.sampleColumn(base.x+x,base.z+z).surfaceHeight;
            // Removing a procedural surface can expose lower saved chunks. Resolve
            // downward without treating a missing resident chunk as sky or air.
            for (;;) {
                if (cancelled()) return std::nullopt;
                const auto address=world::addressOf({base.x+x,height,base.z+z});
                const auto override=read(address.chunk);
                if (!override) { tile[index(x,z)]=height; break; }
                if (!override->blocks || override->coordinate!=address.chunk)
                    throw std::invalid_argument("Invalid sunlight override");
                bool found=false;
                const auto chunkBase=world::blockAt(address.chunk,{}).y;
                for (int y=address.local.y;y>=0;--y) if (world::blocks::opaque(override->blocks->at({x,y,z}))) {
                    tile[index(x,z)]=chunkBase+y; found=true; break;
                }
                if (found || address.chunk.y==world::MinChunkCoord) break;
                height=chunkBase-1;
            }
        }
    }
    for (const auto& snapshot:snapshots) {
        if (cancelled()) return std::nullopt;
        result.include(snapshot);
    }
    return result;
}
std::uint8_t Sunlight::face(world::BlockCoord block,int axis,int sign,world::BlockId material) const {
    if (axis<0 || axis>2 || (sign!=-1 && sign!=1)) throw std::invalid_argument("Invalid sunlight face");
    const bool transparent=world::blocks::transmitsLight(material);
    if (!transparent && axis==1 && sign<0) return 0; // Opaque source roofs its underside.
    auto sample=block;
    if (!transparent && axis!=1) {
        auto& component=axis==0 ? sample.x : sample.z;
        if ((sign>0 && component==std::numeric_limits<std::int64_t>::max()) ||
            (sign<0 && component==std::numeric_limits<std::int64_t>::min())) return 0;
        component+=sign;
    }
    const auto address=world::addressOf(sample);
    const auto found=tops_.find(horizontal(address.chunk));
    if (found==tops_.end()) return 0; // Unknown horizontal boundary is not assumed to be sky.
    const auto top=found->second[index(address.local.x,address.local.z)];
    // +Y samples y+1 without overflowing at INT64_MAX; sides sample air at y.
    return !top || (!transparent && axis==1 ? *top<=block.y : *top<block.y) ? FullSunlight : 0;
}
} // namespace voxel::lighting
