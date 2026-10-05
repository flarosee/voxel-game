#include "lighting/BlockLight.hpp"
#include "lighting/Sunlight.hpp"
#include "world/World.hpp"
#include "world/BlockTypes.hpp"
#include <algorithm>
#include <atomic>
#include <iostream>
#include <random>
#include <thread>

using namespace voxel;
void require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
void equivalent(const lighting::BlockLight& actual,std::span<const world::ChunkSnapshot> snapshots) {
    const auto reference=lighting::BlockLight::build(snapshots).value();
    for (const auto& chunk:snapshots) for (std::size_t cell=0;cell<world::ChunkVolume;++cell) {
        const auto block=world::blockAt(chunk.coordinate,world::localOf(cell));
        require(actual.at(block)==reference.at(block),"Incremental propagation differs from full rebuild");
    }
}
int main() {
    try {
        world::World world;
        for (int x=-1;x<=1;++x) world.allocateChunk({x,0,0});
        auto field=lighting::BlockLight::build(world.snapshots()).value();
        const auto update=[&] {
            const auto snapshots=world.snapshots();
            auto next=field.updated(snapshots);
            require(next.has_value(),"Uncancelled update failed");
            equivalent(*next,snapshots);
            field=std::move(*next);
        };
        world.setBlock({15,8,8},world::blocks::Lamp); update();
        require(field.updateStats().increases>0 && field.updateStats().scannedCells==4096,"Edit did not use changed-chunk diff");
        const auto frozen=field;
        world.setBlock({18,8,8},world::blocks::Lamp); update();
        world.setBlock({15,8,8},world::Air); update();
        require(field.updateStats().decreases>0 && field.at({16,8,8})==13,"Source removal destroyed surviving light");
        require(frozen.at({15,8,8})==15,"Update mutated a published field");
        for (int z=0;z<16;++z) for (int y=0;y<16;++y) world.setBlock({16,y,z},3);
        update(); require(field.at({15,8,8})==0,"Closed wall retained light");
        world.setBlock({16,8,8},world::Air); update();
        require(field.at({15,8,8})==12,"Opening failed to admit light");
        world.setBlock({16,8,8},3); update();
        world.eraseChunk({1,0,0}); update();
        require(field.at({15,8,8})==0 && field.at({18,8,8})==0,"Unloaded source remained in field");
        world.allocateChunk({1,0,0}); world.setBlock({16,8,8},world::blocks::Lamp); update();
        world.eraseChunk({0,0,0}); update();
        world.allocateChunk({0,0,0}); update();
        require(field.at({15,8,8})==14,"Reloaded air did not receive neighboring light");

        auto reordered=world.snapshots(); std::reverse(reordered.begin(),reordered.end());
        auto unchanged=field.updated(reordered).value();
        require(unchanged.updateStats().scannedCells==0 && unchanged.updateStats().processedCells==0,"Unchanged snapshots scheduled propagation");
        world.setBlock({1,1,1},3); update();
        world.setBlock({1,1,1},2); update();
        require(field.updateStats().processedCells==0,"Opaque material-only edit propagated light");

        std::mt19937 random(93821);
        for (int batch=0;batch<60;++batch) {
            for (int edit=0;edit<4;++edit) {
                const world::BlockCoord p{static_cast<int>(random()%40)-8,static_cast<int>(random()%12)+2,static_cast<int>(random()%12)+2};
                const auto choice=random()%5;
                world.setBlock(p,choice==0 ? world::blocks::Lamp : choice<3 ? world::Air : 3);
            }
            if (batch%15==0) world.eraseChunk({-1,0,0});
            if (batch%15==1) world.allocateChunk({-1,0,0});
            update();
        }
        // Cancellation after some queue work must not damage the old field or
        // allow skipped intermediate revisions to leave stale light behind.
        world.setBlock({16,8,8},world::Air);
        const auto finalInputs=world.snapshots();
        int polls=0;
        require(!field.updated(finalInputs,[&]{return ++polls>24;}),"Mid-update cancellation ignored");
        update();
        std::array<std::thread,3> readers;
        std::atomic<bool> stable=true;
        for (auto& thread:readers) thread=std::thread([&]{
            auto next=field.updated(finalInputs);
            if (!next || next->at({15,8,8})!=field.at({15,8,8})) stable=false;
        });
        for (auto& thread:readers) thread.join();
        require(stable,"Concurrent updates disagree");
        world::World distant;
        distant.allocateChunk({world::MaxChunkCoord,0,0});
        distant.setBlock({std::numeric_limits<std::int64_t>::max(),2,2},world::blocks::Lamp);
        field=field.updated(distant.snapshots()).value(); equivalent(field,distant.snapshots());
        require(field.at({0,0,0})==0,"Teleport retained previous residency");

        terrain::TerrainGenerator generator;
        world::World terrainWorld;
        terrainWorld.insertChunk({},generator.generate({}));
        const auto read=[&](world::ChunkCoord c){return terrainWorld.snapshot(c);};
        auto sun=lighting::Sunlight::terrain(generator,terrainWorld.snapshots(),read,[]{return false;}).value();
        require(sun.recomputedColumns()==256,"Initial skylight footprint incorrect");
        auto cached=lighting::Sunlight::terrain(generator,terrainWorld.snapshots(),read,[]{return false;},&sun).value();
        require(cached.recomputedColumns()==0,"Unchanged skylight recomputed columns");
        const world::BlockCoord roof{3,80,3};
        terrainWorld.setBlock(roof,3); cached.invalidate(roof);
        sun=lighting::Sunlight::terrain(generator,terrainWorld.snapshots(),read,[]{return false;},&cached).value();
        require(sun.recomputedColumns()==1 && sun.face({3,generator.sampleColumn(3,3).surfaceHeight,3},1,1)==0,"Single roof edit did not selectively darken column");
        terrainWorld.setBlock(roof,world::Air); sun.invalidate(roof);
        cached=lighting::Sunlight::terrain(generator,terrainWorld.snapshots(),read,[]{return false;},&sun).value();
        require(cached.recomputedColumns()==1 && cached.face({3,generator.sampleColumn(3,3).surfaceHeight,3},1,1)==15,"Selective roof removal retained shadow");
        std::cout << "Incremental lighting matches full rebuilds across edits, overlap, walls, streaming, cancellation, and sunlight invalidation\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
