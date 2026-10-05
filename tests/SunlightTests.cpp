#include "lighting/Sunlight.hpp"
#include "mesh/GreedyMesher.hpp"
#include "world/World.hpp"
#include <iostream>
#include <map>
#include <random>
#include <thread>
#include <atomic>

using namespace voxel;
void require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
lighting::Sunlight sky(std::span<const world::ChunkSnapshot> snapshots) {
    lighting::Sunlight result;
    for (const auto& snapshot:snapshots) result.add(snapshot);
    return result;
}
int main() {
    try {
        world::World w;
        for (int z=-1;z<=1;++z) for (int x=-1;x<=1;++x) w.allocateChunk({x,0,z});
        w.setBlock({0,0,0},1); w.setBlock({1,0,0},1);
        auto open=sky(w.snapshots());
        require(open.face({0,0,0},1,1)==15,"Open sky top must receive full sunlight");
        require(open.face({0,0,0},0,-1)==15,"Exposed adjacent air must receive skylight");
        require(open.face({0,0,0},1,-1)==0,"Opaque block must shade its underside");
        w.setBlock({0,40,0},3);
        auto covered=sky(w.snapshots());
        require(covered.face({0,0,0},1,1)==0,"Roof leaked sunlight across vertical chunks");
        require(covered.face({1,0,0},1,1)==15,"Shadow propagated sideways before that phase exists");
        require(covered.face({0,40,0},1,1)==15,"Roof upper face must remain lit");
        require(open.face({0,0,0},1,1)==15,"Edit mutated old sunlight");
        const auto meshed=mesh::buildOne(w.snapshots(),{0,0,0},&covered);
        int dark=0,lit=0;
        for (const auto& q:meshed.quads) if (q.axis==1 && q.sign==1 && q.origin[1]==1) {
            require(q.width*q.height==1,"Greedy merge crossed a shadow boundary");
            if (q.sunlight==0) ++dark; else if (q.sunlight==15) ++lit;
        }
        require(dark==1 && lit==1,"Shadow boundary did not split into lit/dark faces");
        for (const auto& vertex:meshed.vertices) require(vertex.sunlight==0 || vertex.sunlight==15,"Invalid sunlight vertex level");
        w.setBlock({0,40,0},world::Air);
        require(sky(w.snapshots()).face({0,0,0},1,1)==15,"Rebuild failed to remove roof shadow");
        w.setBlock({-1,-17,-1},1); w.setBlock({-1,32,-1},1);
        const auto negative=sky(w.snapshots());
        require(negative.face({-1,-17,-1},1,1)==0,"Negative coordinate column mismatch");
        require(negative.face({-1,32,-1},1,1)==15,"Negative roof incorrectly dark");
        const auto min=std::numeric_limits<std::int64_t>::min(),max=std::numeric_limits<std::int64_t>::max();
        world::World extreme; extreme.setBlock({max,max,min},1);
        const auto edge=sky(extreme.snapshots());
        require(edge.face({max,max,min},1,1)==15,"Top coordinate limit overflowed");
        require(edge.face({max,max,min},0,1)==0,"Outside domain assumed sky");

        // Independent brute-force vertical-ray oracle for every greedy unit face.
        world::World randomWorld;
        for (int z=-1;z<=1;++z) for (int x=-1;x<=1;++x) randomWorld.allocateChunk({x,0,z});
        std::mt19937 random(1881);
        std::map<std::array<std::int64_t,3>,world::BlockId> solids;
        for (int i=0;i<400;++i) {
            const std::array<std::int64_t,3> p{static_cast<int>(random()%24)-12,static_cast<int>(random()%48)-24,static_cast<int>(random()%24)-12};
            solids[p]=1; randomWorld.setBlock({p[0],p[1],p[2]},1);
        }
        const auto snapshots=randomWorld.snapshots();
        const auto field=sky(snapshots);
        for (const auto& chunk:mesh::build(snapshots,&field)) {
            const auto origin=world::blockAt(chunk.coordinate,{});
            const std::array base{origin.x,origin.y,origin.z};
            for (const auto& q:chunk.quads) for (int v=0;v<q.height;++v) for (int u=0;u<q.width;++u) {
                auto p=base;
                for (int axis=0;axis<3;++axis) p[axis]+=q.origin[axis];
                p[(q.axis+1)%3]+=u; p[(q.axis+2)%3]+=v;
                if (q.sign<0) --p[q.axis]; // Adjacent air cell.
                bool blocked=false;
                for (const auto& [cell,id]:solids) {
                    (void)id;
                    if (cell[0]==p[0] && cell[2]==p[2] && cell[1]>=p[1]) { blocked=true; break; }
                }
                require(q.sunlight==(blocked ? 0 : 15),"Sunlight differs from vertical-ray oracle");
            }
        }
        const auto expected=mesh::build(snapshots,&field);
        std::array<std::thread,4> threads;
        std::atomic<bool> matched=true;
        for (auto& thread:threads) thread=std::thread([&]{
            for (int repeat=0;repeat<4;++repeat) {
                const auto actual=mesh::build(snapshots,&field);
                for (std::size_t i=0;i<actual.size();++i) if (actual[i].vertices!=expected[i].vertices) matched=false;
            }
        });
        for (auto& thread:threads) thread.join();
        require(matched,"Concurrent sunlight changed output");

        // Removed procedural surface exposes the next unmodified layer.
        terrain::TerrainGenerator generator;
        world::World overrides;
        const auto height=generator.sampleColumn(3,3).surfaceHeight;
        const auto surface=world::addressOf({3,height,3}).chunk;
        overrides.allocateChunk(surface,world::Air);
        const auto inputs=overrides.snapshots();
        const auto resolved=lighting::Sunlight::terrain(generator,inputs,[&](auto c){return overrides.snapshot(c);},[]{return false;});
        const auto exposed=world::blockAt(surface,{}).y-1;
        require(resolved && resolved->face({3,exposed,3},1,1)==15,"Removed surface did not expose layer below");
        require(!lighting::Sunlight::terrain(generator,inputs,[&](auto c){return overrides.snapshot(c);},[]{return true;}),"Sunlight ignored cancellation");
        std::cout << "Sunlight occlusion, merge boundaries, ray oracle, limits, cancellation, and concurrency passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
