#include "BlockLight.hpp"
#include "world/BlockTypes.hpp"
#include <algorithm>
#include <limits>
#include <bitset>
#include <deque>

namespace voxel::lighting {
std::uint8_t BlockLight::Tile::get(std::size_t cell) const noexcept {
    return static_cast<std::uint8_t>((packed[cell/2] >> ((cell%2)*4)) & 15);
}
void BlockLight::Tile::set(std::size_t cell,std::uint8_t level) noexcept {
    const auto shift=(cell%2)*4;
    packed[cell/2]=static_cast<std::uint8_t>((packed[cell/2] & ~(15U << shift)) | (level << shift));
}
std::optional<BlockLight> BlockLight::build(std::span<const world::ChunkSnapshot> snapshots,
                                         const std::function<bool()>& cancelled) {
    const auto stop=[&]{return cancelled && cancelled();};
    if (snapshots.size()>std::numeric_limits<std::uint32_t>::max()) throw std::length_error("Too many light chunks");
    BlockLight result;
    result.inputs_.assign(snapshots.begin(),snapshots.end());
    result.tiles_.resize(snapshots.size());
    struct Node { std::uint32_t chunk; std::uint16_t cell; };
    std::vector<Node> queue;
    for (std::size_t i=0;i<snapshots.size();++i) {
        if (stop()) return std::nullopt;
        const auto& snapshot=snapshots[i];
        world::validate(snapshot.coordinate);
        if (!snapshot.blocks || !result.lookup_.emplace(snapshot.coordinate,i).second)
            throw std::invalid_argument("Invalid block-light snapshot set");
        if (snapshot.blocks->occupied()==0) continue;
        for (std::size_t cell=0;cell<world::ChunkVolume;++cell) {
            if (cell%256==0 && stop()) return std::nullopt;
            if (world::blocks::emission(snapshot.blocks->at(cell))!=0) {
                result.tiles_[i].set(cell,15);
                queue.push_back({static_cast<std::uint32_t>(i),static_cast<std::uint16_t>(cell)});
            }
        }
    }
    // All sources are level 15. FIFO visits in increasing distance: the first
    // visit is the brightest, so each air cell enters the queue at most once.
    for (std::size_t cursor=0;cursor<queue.size();++cursor) {
        if (cursor%256==0 && stop()) return std::nullopt;
        const auto node=queue[cursor];
        const auto level=result.tiles_[node.chunk].get(node.cell);
        if (level<=1) continue;
        const auto local=world::localOf(node.cell);
        for (int axis=0;axis<3;++axis) for (int sign:{-1,1}) {
            std::array<int,3> p{local.x,local.y,local.z};
            p[axis]+=sign;
            auto coordinate=snapshots[node.chunk].coordinate;
            std::size_t chunk=node.chunk;
            if (p[axis]<0 || p[axis]>=world::ChunkSide) {
                auto& component=axis==0 ? coordinate.x : axis==1 ? coordinate.y : coordinate.z;
                if ((sign<0 && component==world::MinChunkCoord) || (sign>0 && component==world::MaxChunkCoord)) continue;
                component+=sign;
                const auto found=result.lookup_.find(coordinate);
                if (found==result.lookup_.end()) continue; // Missing data never transmits light.
                chunk=found->second;
                p[axis]=sign<0 ? world::ChunkSide-1 : 0;
            }
            const auto cell=world::indexOf({p[0],p[1],p[2]});
            if (result.tiles_[chunk].get(cell)!=0 || !world::blocks::transmitsLight(snapshots[chunk].blocks->at(cell))) continue;
            result.tiles_[chunk].set(cell,static_cast<std::uint8_t>(level-1));
            queue.push_back({static_cast<std::uint32_t>(chunk),static_cast<std::uint16_t>(cell)});
        }
    }
    return result;
}
std::optional<BlockLight> BlockLight::updated(std::span<const world::ChunkSnapshot> snapshots,
                                            const std::function<bool()>& cancelled) const {
    const auto stop=[&]{return cancelled && cancelled();};
    if (stop()) return std::nullopt;
    if (snapshots.size()>std::numeric_limits<std::uint32_t>::max()) throw std::length_error("Too many light chunks");
    BlockLight next;
    next.inputs_.assign(snapshots.begin(),snapshots.end());
    next.tiles_.resize(snapshots.size());
    for (std::size_t i=0;i<snapshots.size();++i) {
        if (stop()) return std::nullopt;
        const auto& s=snapshots[i];
        world::validate(s.coordinate);
        if (!s.blocks || !next.lookup_.emplace(s.coordinate,i).second) throw std::invalid_argument("Invalid block-light snapshot set");
        if (const auto old=lookup_.find(s.coordinate);old!=lookup_.end()) next.tiles_[i]=tiles_[old->second];
    }
    struct Node { std::uint32_t chunk; std::uint16_t cell; };
    std::deque<Node> queue;
    std::vector<std::bitset<world::ChunkVolume>> pending(snapshots.size());
    const auto enqueue=[&](std::size_t chunk,std::size_t cell) {
        if (!pending[chunk].test(cell)) {
            queue.push_back({static_cast<std::uint32_t>(chunk),static_cast<std::uint16_t>(cell)});
            pending[chunk].set(cell);
        }
    };
    const auto neighbor=[&](Node node,int axis,int sign)->std::optional<Node> {
        const auto local=world::localOf(node.cell);
        std::array<int,3> p{local.x,local.y,local.z}; p[axis]+=sign;
        std::size_t chunk=node.chunk;
        if (p[axis]<0 || p[axis]>=world::ChunkSide) {
            auto c=snapshots[chunk].coordinate;
            auto& component=axis==0 ? c.x : axis==1 ? c.y : c.z;
            if ((sign<0 && component==world::MinChunkCoord) || (sign>0 && component==world::MaxChunkCoord)) return std::nullopt;
            component+=sign;
            const auto found=next.lookup_.find(c);
            if (found==next.lookup_.end()) return std::nullopt;
            chunk=found->second; p[axis]=sign<0 ? world::ChunkSide-1 : 0;
        }
        return Node{static_cast<std::uint32_t>(chunk),static_cast<std::uint16_t>(world::indexOf({p[0],p[1],p[2]}))};
    };
    // Diff only replaced storage; material-only edits with the same optical
    // properties need no propagation. New chunks start dark.
    for (std::size_t i=0;i<snapshots.size();++i) {
        if (stop()) return std::nullopt;
        const auto old=lookup_.find(snapshots[i].coordinate);
        const bool added=old==lookup_.end();
        if (added || inputs_[old->second].blocks!=snapshots[i].blocks) {
            for (std::size_t cell=0;cell<world::ChunkVolume;++cell) {
                if (cell%256==0 && stop()) return std::nullopt;
                ++next.stats_.scannedCells;
                const auto id=snapshots[i].blocks->at(cell);
                const auto before=added ? world::Air : inputs_[old->second].blocks->at(cell);
                if (added || world::blocks::opaque(before)!=world::blocks::opaque(id) || world::blocks::emission(before)!=world::blocks::emission(id)) enqueue(i,cell);
            }
        }
        // Unloaded chunks remove boundary support even though retained block
        // storage is unchanged. Queue the surviving side of each removed seam.
        for (int axis=0;axis<3;++axis) for (int sign:{-1,1}) {
            auto c=snapshots[i].coordinate;
            auto& component=axis==0 ? c.x : axis==1 ? c.y : c.z;
            if ((sign<0 && component==world::MinChunkCoord) || (sign>0 && component==world::MaxChunkCoord)) continue;
            component+=sign;
            if (!lookup_.contains(c) || next.lookup_.contains(c)) continue;
            for (int v=0;v<16;++v) for (int u=0;u<16;++u) {
                std::array<int,3> p{}; p[axis]=sign<0 ? 0 : 15; p[(axis+1)%3]=u; p[(axis+2)%3]=v;
                enqueue(i,world::indexOf({p[0],p[1],p[2]}));
            }
        }
    }
    // Re-evaluate the local light equation for both additions and removals.
    // Strict one-level attenuation prevents self-sustaining cycles after source
    // removal. Requeue neighbors only when a value changes, until convergence.
    while (!queue.empty()) {
        if (next.stats_.processedCells%256==0 && stop()) return std::nullopt;
        const auto node=queue.front(); queue.pop_front(); pending[node.chunk].reset(node.cell);
        ++next.stats_.processedCells;
        const auto id=snapshots[node.chunk].blocks->at(node.cell);
        std::uint8_t desired=world::blocks::emission(id);
        if (world::blocks::transmitsLight(id)) for (int axis=0;axis<3;++axis) for (int sign:{-1,1}) {
            if (const auto n=neighbor(node,axis,sign)) {
                const auto value=next.tiles_[n->chunk].get(n->cell);
                if (value>0) desired=std::max(desired,static_cast<std::uint8_t>(value-1));
            }
        }
        const auto before=next.tiles_[node.chunk].get(node.cell);
        if (desired==before) continue;
        if (desired>before) ++next.stats_.increases; else ++next.stats_.decreases;
        next.tiles_[node.chunk].set(node.cell,desired);
        for (int axis=0;axis<3;++axis) for (int sign:{-1,1})
            if (const auto n=neighbor(node,axis,sign)) enqueue(n->chunk,n->cell);
    }
    if (stop()) return std::nullopt;
    return next;
}
std::uint8_t BlockLight::at(world::BlockCoord block) const {
    const auto address=world::addressOf(block);
    const auto found=lookup_.find(address.chunk);
    return found==lookup_.end() ? 0 : tiles_[found->second].get(world::indexOf(address.local));
}
std::uint8_t BlockLight::face(world::BlockCoord block,int axis,int sign,world::BlockId material) const {
    if (axis<0 || axis>2 || (sign!=-1 && sign!=1)) throw std::invalid_argument("Invalid block-light face");
    const auto emission=world::blocks::transmitsLight(material) ? at(block) : world::blocks::emission(material);
    auto& component=axis==0 ? block.x : axis==1 ? block.y : block.z;
    if ((sign<0 && component==std::numeric_limits<std::int64_t>::min()) ||
        (sign>0 && component==std::numeric_limits<std::int64_t>::max())) return emission;
    component+=sign;
    return std::max(emission,at(block));
}
} // namespace voxel::lighting
