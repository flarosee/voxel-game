#include "mesh/Transparency.hpp"
#include "world/World.hpp"
#include "world/BlockTypes.hpp"
#include <cmath>
#include <iostream>
#include <random>

using namespace voxel;
namespace {
void require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
using Vec=std::array<double,3>;
Vec sub(Vec a,Vec b) { for(int i=0;i<3;++i) a[i]-=b[i]; return a; }
Vec cross(Vec a,Vec b) { return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]}; }
double dot(Vec a,Vec b) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
Vec point(const mesh::Vertex& v) { return {v.position[0],v.position[1],v.position[2]}; }
double area(std::span<const mesh::Vertex> vertices) {
    double result=0;
    for(std::size_t i=0;i<vertices.size();i+=3) {
        const auto n=cross(sub(point(vertices[i+1]),point(vertices[i])),sub(point(vertices[i+2]),point(vertices[i])));
        result+=std::sqrt(dot(n,n))/2;
    }
    return result;
}
void rayOrder(const mesh::Transparency& tree,const std::vector<mesh::Vertex>& vertices,Vec eye,Vec direction) {
    const auto ranges=tree.backToFront({static_cast<float>(eye[0]),static_cast<float>(eye[1]),static_cast<float>(eye[2])});
    double previous=1e30;
    std::size_t count=0;
    for(const auto range:ranges) {
        count+=range.count;
        require(range.first+range.count<=vertices.size(),"Draw outside vertex buffer");
        for(std::size_t i=range.first;i<range.first+range.count;i+=3) {
            const auto a=point(vertices[i]);
            const auto e1=sub(point(vertices[i+1]),a), e2=sub(point(vertices[i+2]),a);
            const auto h=cross(direction,e2);
            const auto determinant=dot(e1,h);
            if(std::abs(determinant)<1e-9) continue;
            const auto s=sub(eye,a);
            const double u=dot(s,h)/determinant;
            const auto q=cross(s,e1);
            const double v=dot(direction,q)/determinant;
            if(u<0 || v<0 || u+v>1) continue;
            const double t=dot(e2,q)/determinant;
            if(t<=0) continue;
            require(t<=previous+1e-5,"Transparent ray is not ordered back to front");
            previous=t;
        }
    }
    require(count==vertices.size(),"Transparent traversal omitted or duplicated vertices");
}
}
int main() {
    try {
        // All three axes, both sides of negative and positive chunk seams.
        for(int axis=0;axis<3;++axis) for(int boundary:{-16,0,16})
        for(world::BlockId a:{world::blocks::Water,world::blocks::Glass,world::blocks::Leaves})
        for(world::BlockId b:{world::BlockId(3),world::blocks::Water,world::blocks::Glass,world::blocks::Leaves}) {
            world::World world;
            std::array<std::int64_t,3> p{2,2,2}; p[axis]=boundary-1;
            world.setBlock({p[0],p[1],p[2]},a); ++p[axis];
            world.setBlock({p[0],p[1],p[2]},b);
            auto meshes=mesh::build(world.snapshots());
            int faces=0;
            for(const auto& m:meshes) for(const auto& q:m.quads) faces+=q.width*q.height;
            const int expected=a==b ? 10 : (b==3 || (a!=world::blocks::Leaves && b!=world::blocks::Leaves)) ? 11 : 12;
            require(faces==expected,"Incorrect transparent neighbor face count");
            world.eraseChunk(world::addressOf({p[0],p[1],p[2]}).chunk);
            faces=0;
            for(const auto& m:mesh::build(world.snapshots())) for(const auto& q:m.quads) faces+=q.width*q.height;
            require(faces==6,"Unloaded neighbor did not restore boundary faces");
        }
        world::World merged;
        for(int x=0;x<16;++x) for(int z=0;z<16;++z) merged.setBlock({x,0,z},world::blocks::Water);
        auto m=mesh::build(merged.snapshots()).front();
        require(m.quads.size()==6,"Water slab failed greedy merging");
        // Intersecting large rectangles force splitting. Independent ray/triangle
        // intersections test actual draw order, not the partition implementation.
        std::vector<mesh::Quad> quads;
        std::mt19937 random(72315);
        double expectedArea=0;
        for(int i=0;i<80;++i) {
            mesh::Quad q{{static_cast<int>(random()%16),static_cast<int>(random()%16),static_cast<int>(random()%16)},
                static_cast<int>(random()%3),i%2 ? 1:-1,static_cast<int>(random()%8)+1,static_cast<int>(random()%8)+1,
                i%2 ? world::blocks::Water:world::blocks::Glass};
            expectedArea+=q.width*q.height; quads.push_back(q);
        }
        std::vector<mesh::Vertex> vertices;
        auto tree=mesh::buildTransparency(quads,vertices,1024*1024).value();
        require(std::abs(area(vertices)-expectedArea)<1e-5,"BSP split lost or duplicated area");
        for(const auto& v:vertices) {
            int axis=0; while(axis<2 && v.normal[axis]==0) ++axis;
            require(v.uv[0]==v.position[(axis+1)%3]*v.normal[axis] && v.uv[1]==v.position[(axis+2)%3],"Split changed UV phase");
        }
        for(int i=0;i<1500;++i) {
            Vec eye{},target{};
            for(int a=0;a<3;++a) { eye[a]=static_cast<int>(random()%6000)/100.0-20; target[a]=static_cast<int>(random()%2400)/100.0; }
            rayOrder(tree,vertices,eye,sub(target,eye));
        }
        vertices.clear();
        require(!mesh::buildTransparency(quads,vertices,1024*1024,[]{return true;}),"Cancelled BSP was published");
        bool rejected=false;
        try { mesh::buildTransparency(quads,vertices,12); } catch(const std::length_error&) { rejected=true; }
        require(rejected,"Fragment budget ignored");
        world::World lights;
        lights.allocateChunk({}); lights.setBlock({1,1,1},world::blocks::Lamp);
        auto field=lighting::BlockLight::build(lights.snapshots()).value();
        for(auto material:{world::blocks::Glass,world::blocks::Water,world::blocks::Leaves,world::BlockId(3)}) {
            lights.setBlock({2,1,1},material);
            field=field.updated(lights.snapshots()).value();
            const auto reference=lighting::BlockLight::build(lights.snapshots()).value();
            require(field.at({2,1,1})==(material==3 ? 0:14),"Incorrect transparent light transmission");
            for(std::size_t i=0;i<world::ChunkVolume;++i) {
                const auto p=world::blockAt({},world::localOf(i));
                require(field.at(p)==reference.at(p),"Opacity update differs from rebuild");
            }
        }
        lights.setBlock({2,10,1},world::blocks::Glass);
        lighting::Sunlight sun; for(const auto& s:lights.snapshots()) sun.add(s);
        require(sun.face({2,1,1},1,1)==15 && sun.face({2,10,1},1,-1,world::blocks::Glass)==15,"Glass blocks skylight");
        std::cout<<"Transparent interfaces, seams, greedy merging, BSP ray order, UVs, budgets, and lighting passed\n";
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
