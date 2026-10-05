#include "mesh/GreedyMesher.hpp"
#include "terrain/TerrainGenerator.hpp"
#include "world/World.hpp"
#include <atomic>
#include <cmath>
#include <iostream>
#include <map>
#include <random>
#include <set>
#include <thread>
#include <tuple>

using namespace voxel;
namespace {
void require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F&& f) {
    bool rejected=false;
    try { f(); } catch (const std::exception&) { rejected=true; }
    require(rejected,"Invalid mesher input accepted");
}
using Point = std::array<std::int64_t,3>;
using Face = std::tuple<Point,int,int,world::BlockId>;
using Edge = std::pair<Point,Point>;

std::set<Face> reference(std::span<const world::ChunkSnapshot> snapshots) {
    // Independent oracle: a flat map of occupied WORLD cells, six neighbors each.
    std::map<Point,world::BlockId> blocks;
    for (const auto& chunk : snapshots) for (std::size_t i=0; i<world::ChunkVolume; ++i) {
        const auto id=chunk.blocks->at(i);
        if (id == world::Air) continue;
        const auto p=world::blockAt(chunk.coordinate,world::localOf(i));
        blocks[{p.x,p.y,p.z}]=id;
    }
    std::set<Face> result;
    for (const auto& [point,id] : blocks) for (int axis=0; axis<3; ++axis) for (int sign : {-1,1}) {
        auto adjacent=point;
        adjacent[axis]+=sign;
        if (!blocks.contains(adjacent)) result.emplace(point,axis,sign,id);
    }
    return result;
}

void verify(std::span<const world::ChunkSnapshot> snapshots, bool closedManifold=false) {
    const auto meshes=mesh::build(snapshots);
    std::set<Face> actual;
    std::map<Edge,unsigned> edges;
    for (const auto& chunk : meshes) {
        const auto origin=world::blockAt(chunk.coordinate,{});
        const Point base{origin.x,origin.y,origin.z};
        std::size_t cursor=0;
        for (const auto& quad : chunk.quads) {
            require(quad.axis>=0 && quad.axis<3 && (quad.sign==-1 || quad.sign==1),"Invalid face direction");
            const auto u=(quad.axis+1)%3, v=(quad.axis+2)%3;
            require(quad.width>0 && quad.height>0 && quad.origin[u]+quad.width<=16 &&
                    quad.origin[v]+quad.height<=16,"Quad exceeds chunk");
            for (int y=0; y<quad.height; ++y) for (int x=0; x<quad.width; ++x) {
                Point point{base[0]+quad.origin[0],base[1]+quad.origin[1],base[2]+quad.origin[2]};
                point[u]+=x; point[v]+=y;
                if (quad.sign>0) --point[quad.axis];
                require(actual.emplace(point,quad.axis,quad.sign,quad.material).second,"Duplicate exposed face");
            }
            const auto vertexCount=static_cast<std::size_t>(6*(quad.width+quad.height));
            require(cursor+vertexCount<=chunk.vertices.size(),"Missing quad triangles");
            double area=0;
            for (std::size_t i=cursor; i<cursor+vertexCount; i+=3) {
                const auto& a=chunk.vertices[i];
                const auto& b=chunk.vertices[i+1];
                const auto& c=chunk.vertices[i+2];
                std::array<double,3> ab{},ac{};
                for (int axis=0; axis<3; ++axis) { ab[axis]=b.position[axis]-a.position[axis]; ac[axis]=c.position[axis]-a.position[axis]; }
                const std::array<double,3> cross{ab[1]*ac[2]-ab[2]*ac[1],ab[2]*ac[0]-ab[0]*ac[2],ab[0]*ac[1]-ab[1]*ac[0]};
                require(cross[quad.axis]*quad.sign>0,"Degenerate or inward triangle winding");
                area+=cross[quad.axis]*quad.sign*0.5;
                for (std::size_t j=i; j<i+3; ++j) {
                    const auto& vertex=chunk.vertices[j];
                    require(vertex.material==quad.material,"Material changed within merged face");
                    for (int axis=0; axis<3; ++axis) {
                        require(vertex.position[axis]>=0 && vertex.position[axis]<=16,"Vertex outside chunk");
                        require(vertex.normal[axis]==(axis==quad.axis ? static_cast<float>(quad.sign) : 0.0F),"Incorrect normal");
                    }
                    require(vertex.position[quad.axis]==static_cast<float>(quad.origin[quad.axis]),"Non-planar quad");
                    require(vertex.uv[0]==quad.sign*vertex.position[u] && vertex.uv[1]==vertex.position[v],
                            "UV orientation or voxel scale incorrect");
                    Point first{},second{};
                    const auto& next=chunk.vertices[i+(j-i+1)%3];
                    for (int axis=0; axis<3; ++axis) {
                        // Exact doubled integer positions include half-integer fan centers.
                        first[axis]=base[axis]*2+static_cast<std::int64_t>(vertex.position[axis]*2);
                        second[axis]=base[axis]*2+static_cast<std::int64_t>(next.position[axis]*2);
                    }
                    if (second<first) std::swap(first,second);
                    ++edges[{first,second}];
                }
            }
            require(area==quad.width*quad.height,"Triangulation does not cover quad area exactly");
            cursor+=vertexCount;
        }
        require(cursor==chunk.vertices.size(),"Extra triangles without a quad");
    }
    require(actual==reference(snapshots),"Greedy surfaces differ from independent unit-face oracle");
    if (closedManifold) for (const auto& [edge,count] : edges) {
        (void)edge;
        require(count==2,"Open edge or T-junction in a closed test solid");
    }
}

void fixtures() {
    world::World world;
    world.allocateChunk({0,0,0});
    require(mesh::build(world.snapshots())[0].vertices.empty(),"Air produced triangles");
    world.setBlock({4,4,4},1);
    require(mesh::build(world.snapshots())[0].quads.size()==6,"Single cube must have six quads");
    verify(world.snapshots(),true);
    world.insertChunk({0,0,0},world::BlockStorage(1),true);
    require(mesh::build(world.snapshots())[0].quads.size()==6,"Solid chunk did not greedily merge to six rectangles");
    verify(world.snapshots(),true);
    world.allocateChunk({1,0,0},2);
    const auto pair=mesh::build(world.snapshots());
    require(pair[0].quads.size()+pair[1].quads.size()==10,"Solid neighbors retained their internal interface");
    verify(world.snapshots(),true);

    world::World cavity;
    for (int z=-1; z<=1; ++z) for (int y=-1; y<=1; ++y) for (int x=-1; x<=1; ++x)
        if (x!=0 || y!=0 || z!=0) cavity.setBlock({x,y,z},1);
    verify(cavity.snapshots(),true);
    // Different rectangle sizes on both sides of a negative chunk boundary.
    world::World stairs;
    for (int z=-3; z<4; ++z) for (int x=-4; x<5; ++x) for (int y=0; y<2+(x+4)/2; ++y)
        stairs.setBlock({x,y,z},static_cast<world::BlockId>(1+(z+3)%2));
    verify(stairs.snapshots(),true);
    world::World checker;
    for (int z=0; z<8; ++z) for (int x=0; x<8; ++x)
        checker.setBlock({x,0,z},static_cast<world::BlockId>(1+(x+z)%2));
    verify(checker.snapshots(),true);
    const auto checkerMesh=mesh::build(checker.snapshots());
    unsigned topQuads=0;
    for (const auto& q : checkerMesh[0].quads) if (q.axis==1 && q.sign==1) ++topQuads;
    require(topQuads==64,"Different materials merged together");
}

void editsAndLifetimes() {
    world::World world;
    world.allocateChunk({-1,0,0},1);
    auto before=world.snapshots();
    const auto oldMesh=mesh::build(before);
    require(mesh::sameSnapshots(before,world.snapshots()),"Unchanged scene spuriously dirty");
    world.allocateChunk({0,0,0},1);
    auto loaded=world.snapshots();
    require(!mesh::sameSnapshots(before,loaded),"Neighbor load did not invalidate meshes");
    verify(loaded,true);
    world.setBlock({0,5,5},world::Air);
    auto edited=world.snapshots();
    require(!mesh::sameSnapshots(loaded,edited),"Boundary removal did not invalidate meshes");
    verify(edited,true);
    world.setBlock({0,5,5},3);
    verify(world.snapshots(),true);
    world.eraseChunk({0,0,0});
    require(!mesh::sameSnapshots(edited,world.snapshots()),"Neighbor unload did not invalidate meshes");
    verify(world.snapshots(),true);
    require(mesh::build(before)[0].vertices==oldMesh[0].vertices,"Edits changed an old mesh input snapshot");
    world.insertChunk({-1,0,0},world::BlockStorage(world::Air),true);
    require(mesh::build(world.snapshots())[0].vertices.empty(),"Removing last blocks left stale geometry");
    auto duplicate=before;
    duplicate.push_back(before[0]);
    rejects([&] { (void)mesh::build(duplicate); });
    std::array<world::ChunkSnapshot,1> invalid{{{{0,0,0},nullptr}}};
    rejects([&] { (void)mesh::build(invalid); });
    // Coordinate bounds must not wrap neighbor lookup to the other end of the world.
    world::World extremes;
    extremes.allocateChunk({world::MinChunkCoord,0,0},1);
    extremes.allocateChunk({world::MaxChunkCoord,0,0},1);
    for (const auto& chunk : mesh::build(extremes.snapshots())) require(chunk.quads.size()==6,"Neighbor address overflow");
}

void randomAndTerrain() {
    std::mt19937 random(7721);
    for (unsigned trial=0; trial<12; ++trial) {
        world::World world;
        for (unsigned edit=0; edit<160; ++edit)
            world.setBlock({static_cast<int>(random()%24)-12,static_cast<int>(random()%8)-4,
                            static_cast<int>(random()%24)-12},static_cast<world::BlockId>(1+random()%4));
        verify(world.snapshots());
    }
    terrain::TerrainGenerator generator;
    world::World terrainWorld;
    for (int z=-1; z<=0; ++z) for (int y=-1; y<=0; ++y) for (int x=-1; x<=0; ++x)
        terrainWorld.insertChunk({x,y,z},generator.generate({x,y,z}));
    verify(terrainWorld.snapshots(),true);
}

void concurrent() {
    world::World world;
    world.allocateChunk({0,0,0},1);
    const auto stable=world.snapshots();
    const auto expected=mesh::build(stable)[0].vertices;
    std::atomic<bool> okay{true};
    std::vector<std::jthread> workers;
    for (int worker=0; worker<4; ++worker) workers.emplace_back([&] {
        try {
            for (int i=0; i<12; ++i) if (mesh::build(stable)[0].vertices!=expected) okay=false;
        } catch (...) { okay=false; }
    });
    for (int i=0; i<300; ++i) world.setBlock({i%16,0,0},static_cast<world::BlockId>(i%3));
    world.eraseChunk({0,0,0});
    workers.clear();
    require(okay,"Concurrent meshing or snapshot lifetime failed");
}
}
int main() {
    try {
        fixtures(); editsAndLifetimes(); randomAndTerrain(); concurrent();
        std::cout << "Greedy coverage, normals, UVs, winding, conforming seams, edits, and lifetime checks passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
