#include "lighting/BlockLight.hpp"
#include "mesh/GreedyMesher.hpp"
#include "world/World.hpp"
#include "world/BlockTypes.hpp"
#include <atomic>
#include <iostream>
#include <queue>
#include <random>
#include <thread>

using namespace voxel;
void require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
int main() {
    try {
        world::World open;
        for (int z=-1;z<=1;++z) for (int y=-1;y<=1;++y) for (int x=-1;x<=1;++x) open.allocateChunk({x,y,z});
        open.setBlock({15,15,15},world::blocks::Lamp);
        auto light=lighting::BlockLight::build(open.snapshots()).value();
        for (int z=0;z<=30;++z) for (int y=0;y<=30;++y) for (int x=0;x<=30;++x)
            require(light.at({x,y,z})==std::max(0,15-std::abs(x-15)-std::abs(y-15)-std::abs(z-15)),"Incorrect attenuation or positive chunk seam");
        require(light.at({30,15,15})==0 && light.at({29,15,15})==1,"Light range must end after level 1");
        require(light.face({15,15,15},1,-1,world::blocks::Lamp)==15,"Lamp underside must emit");
        open.setBlock({15,15,15},world::Air);
        require(lighting::BlockLight::build(open.snapshots())->at({16,15,15})==0,"Removed source left stale light");
        require(light.at({16,15,15})==14,"Edit mutated old light field");
        open.setBlock({-1,-1,-1},world::blocks::Lamp);
        require(lighting::BlockLight::build(open.snapshots())->at({0,0,0})==12,"Negative chunk crossing failed");

        world::World tunnel;
        tunnel.allocateChunk({},3);
        for (int x=1;x<=5;++x) tunnel.setBlock({x,1,1},world::Air);
        for (int z=1;z<=5;++z) tunnel.setBlock({5,1,z},world::Air);
        tunnel.setBlock({1,1,1},world::blocks::Lamp);
        auto bent=lighting::BlockLight::build(tunnel.snapshots()).value();
        require(bent.at({5,1,5})==7,"Light must follow air around corners");
        require(bent.at({1,1,2})==0,"Opaque wall transmitted light");
        tunnel.setBlock({5,1,3},65535);
        require(lighting::BlockLight::build(tunnel.snapshots())->at({5,1,5})==0,"Sealed tunnel leaked light");
        tunnel.setBlock({5,1,5},world::blocks::Lamp);
        require(lighting::BlockLight::build(tunnel.snapshots())->at({5,1,4})==14,"Second source failed behind wall");

        // Independent per-source shortest-path oracle in a randomly obstructed room.
        world::World room;
        room.allocateChunk({},3);
        std::mt19937 random(7189);
        for (int z=1;z<7;++z) for (int y=1;y<7;++y) for (int x=1;x<7;++x)
            if (random()%4) room.setBlock({x,y,z},world::Air);
        const std::array<world::BlockCoord,3> sources{{{1,1,1},{6,6,6},{1,6,3}}};
        for (const auto source:sources) room.setBlock(source,world::blocks::Lamp);
        std::array<int,4096> expected{};
        for (const auto source:sources) {
            std::array<int,4096> distance; distance.fill(999);
            std::queue<world::BlockCoord> queue;
            queue.push(source); distance[world::indexOf(world::addressOf(source).local)]=0;
            while (!queue.empty()) {
                const auto p=queue.front(); queue.pop();
                const auto index=world::indexOf(world::addressOf(p).local);
                expected[index]=std::max(expected[index],std::max(0,15-distance[index]));
                for (const auto d:std::array<world::BlockCoord,6>{{{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}}}) {
                    const world::BlockCoord next{p.x+d.x,p.y+d.y,p.z+d.z};
                    if (next.x<1 || next.x>6 || next.y<1 || next.y>6 || next.z<1 || next.z>6 || room.getBlock(next)!=world::Air) continue;
                    auto& nextDistance=distance[world::indexOf(world::addressOf(next).local)];
                    if (nextDistance!=999) continue;
                    nextDistance=distance[index]+1; queue.push(next);
                }
            }
        }
        const auto snapshots=room.snapshots();
        const auto field=lighting::BlockLight::build(snapshots).value();
        for (std::size_t i=0;i<4096;++i) require(field.at(world::blockAt({},world::localOf(i)))==expected[i],"Block light differs from independent graph-distance oracle");
        const auto meshes=mesh::build(snapshots,nullptr,&field);
        for (const auto& chunk:meshes) for (const auto& quad:chunk.quads)
            for (int v=0;v<quad.height;++v) for (int u=0;u<quad.width;++u) {
                auto cell=quad.origin;
                cell[(quad.axis+1)%3]+=u; cell[(quad.axis+2)%3]+=v;
                if (quad.sign>0) --cell[quad.axis];
                require(quad.blockLight==field.face(world::blockAt(chunk.coordinate,{cell[0],cell[1],cell[2]}),quad.axis,quad.sign,quad.material),"Greedy rectangle merged unequal block light");
            }
        std::array<std::thread,4> readers;
        std::atomic<bool> stable=true;
        for (auto& thread:readers) thread=std::thread([&]{
            const auto rebuilt=lighting::BlockLight::build(snapshots).value();
            if (mesh::build(snapshots,nullptr,&rebuilt)[0].vertices!=meshes[0].vertices) stable=false;
        });
        for (auto& thread:readers) thread.join();
        require(stable,"Concurrent lighting builds disagree");
        require(!lighting::BlockLight::build(snapshots,[]{return true;}),"Build ignored cancellation");
        auto duplicates=snapshots; duplicates.push_back(snapshots[0]);
        bool rejected=false; try { (void)lighting::BlockLight::build(duplicates); } catch (const std::invalid_argument&) { rejected=true; }
        require(rejected,"Duplicate light chunks accepted");
        world::World edge;
        const auto max=std::numeric_limits<std::int64_t>::max();
        edge.setBlock({max,max,max},world::blocks::Lamp);
        const auto extreme=lighting::BlockLight::build(edge.snapshots()).value();
        require(extreme.at({max-1,max,max})==14 && extreme.face({max,max,max},0,1,world::blocks::Lamp)==15,"Coordinate limit overflowed");
        std::cout << "Block light attenuation, seams, occlusion, multisource oracle, meshing, cancellation, and concurrency passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
