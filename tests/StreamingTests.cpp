#include "streaming/ChunkStreamer.hpp"
#include "world/BlockTypes.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace voxel;
using namespace std::chrono_literals;
void require(bool condition,const char* text) { if (!condition) throw std::runtime_error(text); }
std::shared_ptr<const streaming::Scene> wait(streaming::ChunkStreamer& streamer) {
    const auto until=std::chrono::steady_clock::now()+20s;
    while (std::chrono::steady_clock::now()<until) {
        if (auto scene=streamer.takeReady()) {
            require(scene->revision==streamer.revision(),"Stale mesh published");
            return scene;
        }
        std::this_thread::sleep_for(1ms);
    }
    throw std::runtime_error("Worker completion timed out");
}
void bounds(streaming::ChunkStreamer& s, std::size_t limit) {
    const auto stats=s.stats();
    require(stats.resident<=limit,"Residency exceeded bound");
    require(stats.commands<=32 && stats.generationPending<=1 && stats.meshPending<=1 && stats.ready<=1,"Queue exceeded bound");
}
std::uint32_t topLight(const streaming::Scene& scene,world::BlockCoord block,bool blockChannel=false) {
    const auto address=world::addressOf(block);
    const double x=static_cast<double>((address.chunk.x-scene.origin.x)*16+address.local.x)+0.37;
    const double z=static_cast<double>((address.chunk.z-scene.origin.z)*16+address.local.z)+0.29;
    const float y=static_cast<float>((address.chunk.y-scene.origin.y)*16+address.local.y+1);
    for (std::size_t i=0;i<scene.vertices.size();i+=3) {
        const auto& a=scene.vertices[i]; const auto& b=scene.vertices[i+1]; const auto& c=scene.vertices[i+2];
        if (a.normal[1]!=1 || a.position[1]!=y) continue;
        const auto side=[&](const auto& p,const auto& q) {
            return (q.position[0]-p.position[0])*(z-p.position[2])-(q.position[2]-p.position[2])*(x-p.position[0]);
        };
        const auto ab=side(a,b),bc=side(b,c),ca=side(c,a);
        if ((ab>=0 && bc>=0 && ca>=0) || (ab<=0 && bc<=0 && ca<=0)) return blockChannel ? a.blockLight : a.sunlight;
    }
    throw std::runtime_error("Missing sampled terrain surface");
}
int main() {
    const auto root=std::filesystem::temp_directory_path()/("voxel-stream-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        std::filesystem::create_directories(root);
        const world::ChunkCoord home{0,-1,0};
        const auto block=world::blockAt(home,{1,0,1});
        std::vector<mesh::Vertex> baseline;
        {
            streaming::ChunkStreamer stream(12345,root,0);
            stream.request(home);
            const auto first=wait(stream);
            baseline=first->vertices;
            const auto reference=mesh::buildOne(stream.world().snapshots(),home);
            require(reference.vertices==baseline,"Worker mesh differs from halo reference");
            // Empty adjacent face boundaries are determined from the entire halo.
            require(stream.world().chunkCount()==27,"Generation halo incomplete");
            const auto old=stream.world().getBlock(block);
            require(old!=world::Air,"Edit fixture must start solid");
            require(stream.edit(block,old,world::Air),"Edit command rejected");
            wait(stream);
            require(stream.world().getBlock(block)==world::Air,"Worker edit not applied");
            for (int step=1;step<=24;++step) {
                stream.request({step*3,-1,-step*3});
                wait(stream); bounds(stream,27);
            }
            stream.request(home); wait(stream);
            require(stream.world().getBlock(block)==world::Air,"Eviction lost edit");
            require(stream.exportChunk(home),"Export rejected"); wait(stream);
            require(stream.regenerate(home),"Regenerate rejected");
            require(wait(stream)->vertices==baseline,"Regeneration differs from original");
            require(stream.importChunk(),"Import rejected"); wait(stream);
            require(stream.world().getBlock(block)==world::Air,"Asynchronous import lost edit");
            // Latest request wins; superseded jobs and meshes never accumulate.
            std::thread movement([&] { for (int step=0;step<2000;++step) stream.request({step*17,-1,-step*13}); });
            for (int i=0;i<100;++i) { bounds(stream,27); (void)stream.takeReady(); }
            movement.join();
            const world::ChunkCoord distant{INT64_C(1)<<50,-1,-(INT64_C(1)<<50)};
            stream.request(distant);
            const auto far=wait(stream);
            require(far->origin==distant,"Teleport returned obsolete scene");
            for (const auto& vertex:far->vertices) for (float component:vertex.position)
                require(component>=0 && component<=16,"Mesh positions must remain local at huge coordinates");
            bounds(stream,27);
            stream.request(home); wait(stream);
            stream.close();
        }
        {
            streaming::ChunkStreamer reopened(12345,root,0);
            reopened.request(home); wait(reopened);
            require(reopened.world().getBlock(block)==world::Air,"Restart lost persisted edit");
            reopened.request({world::MinChunkCoord,world::MinChunkCoord,world::MaxChunkCoord});
            wait(reopened); bounds(reopened,27);
            reopened.close();
        }
        {
            streaming::ChunkStreamer full(9876,root,2);
            full.request({0,0,0});
            const auto scene=wait(full);
            require(full.world().chunkCount()==343,"Full streaming halo incomplete");
            require(scene->vertices.size()<=streaming::MaxSceneVertices,"Vertex budget exceeded");
            bounds(full,343);
            // Adjacent output chunks use one captured halo, same as the trusted mesher.
            const auto snapshots=full.world().snapshots();
            std::size_t referenceCount=0;
            for (int z=-2;z<=2;++z) for (int y=-2;y<=2;++y) for (int x=-2;x<=2;++x)
                referenceCount+=mesh::buildOne(snapshots,{x,y,z}).vertices.size();
            require(referenceCount==scene->vertices.size(),"Streaming dropped chunk geometry");
            full.close();
        }
        // Roof far above the loaded halo must shadow terrain, even after eviction,
        // restart, and recovery of a saved .bak file. Removal restores sky exposure.
        const terrain::TerrainGenerator sunGenerator(999);
        const world::BlockCoord ground{3,sunGenerator.sampleColumn(3,3).surfaceHeight,3};
        const world::BlockCoord roof{3,ground.y+128,3};
        const auto groundChunk=world::addressOf(ground).chunk,roofChunk=world::addressOf(roof).chunk;
        {
            streaming::ChunkStreamer sun(999,root,0);
            sun.request(groundChunk);
            require(topLight(*wait(sun),ground)==15,"Open terrain unexpectedly dark");
            sun.request(roofChunk); wait(sun);
            require(sun.edit(roof,world::Air,3),"Roof placement rejected"); wait(sun);
            sun.request(groundChunk);
            require(topLight(*wait(sun),ground)==0,"Unloaded roof leaked sunlight");
            sun.close();
        }
        const auto roofFile=root/"v1-seed-999"/(std::to_string(roofChunk.x)+"_"+std::to_string(roofChunk.y)+"_"+std::to_string(roofChunk.z)+".vxc");
        auto backup=roofFile; backup += ".bak";
        std::filesystem::rename(roofFile,backup);
        {
            streaming::ChunkStreamer sun(999,root,0);
            sun.request(groundChunk);
            require(topLight(*wait(sun),ground)==0,"Restart/backup recovery lost sky occluder");
            sun.request(roofChunk); wait(sun);
            require(sun.edit(roof,3,world::Air),"Roof removal rejected"); wait(sun);
            sun.request(groundChunk);
            require(topLight(*wait(sun),ground)==15,"Removed unloaded roof left stale shadow");
            sun.close();
        }
        // A lamp in the generation halo (outside the drawn chunk) must illuminate
        // the visible seam, persist across eviction/restart, and clear on removal.
        const terrain::TerrainGenerator lampGenerator(1001);
        const world::BlockCoord litGround{15,lampGenerator.sampleColumn(15,3).surfaceHeight,3};
        const world::BlockCoord lamp{16,std::max(litGround.y,lampGenerator.sampleColumn(16,3).surfaceHeight)+2,3};
        const auto lampCenter=world::addressOf(litGround).chunk;
        const auto expectedLight=static_cast<std::uint32_t>(15-(1+lamp.y-litGround.y-1));
        {
            streaming::ChunkStreamer lamps(1001,root,0);
            lamps.request(lampCenter);
            require(topLight(*wait(lamps),litGround,true)==0,"No-source scene contains block light");
            require(lamps.edit(lamp,world::Air,world::blocks::Lamp),"Halo lamp placement rejected");
            auto scene=wait(lamps);
            require(topLight(*scene,litGround,true)==expectedLight,"Halo source failed to illuminate visible seam");
            require(topLight(*scene,litGround)==15,"Block lighting changed the sunlight channel");
            lamps.request({30,lampCenter.y,30}); wait(lamps);
            lamps.request(lampCenter);
            require(topLight(*wait(lamps),litGround,true)==expectedLight,"Returning to saved lamp changed light");
            lamps.close();
        }
        {
            streaming::ChunkStreamer lamps(1001,root,0);
            lamps.request(lampCenter);
            require(topLight(*wait(lamps),litGround,true)==expectedLight,"Restart lost lamp emission");
            require(lamps.edit(lamp,world::blocks::Lamp,world::Air),"Lamp removal rejected");
            require(topLight(*wait(lamps),litGround,true)==0,"Removed saved lamp left stale light");
            lamps.close();
        }
        // Material edits move geometry between passes; saved IDs and partition
        // metadata must survive a residency change and restart.
        const world::BlockCoord transparentBlock{15,100,3};
        const auto transparentCenter=world::addressOf(transparentBlock).chunk;
        {
            streaming::ChunkStreamer transparent(123,root,0);
            transparent.request(transparentCenter); wait(transparent);
            require(transparent.edit(transparentBlock,world::Air,world::blocks::Glass),"Glass edit rejected");
            auto scene=wait(transparent);
            require(scene->opaqueCount==0 && scene->cutoutCount==0 && !scene->transparency.nodes.empty(),"Glass in wrong render pass");
            require(transparent.edit(transparentBlock,world::blocks::Glass,world::blocks::Leaves),"Leaf edit rejected");
            scene=wait(transparent);
            require(scene->cutoutCount==scene->vertices.size() && scene->transparency.nodes.empty(),"Cutout edit retained old blend geometry");
            require(transparent.edit(transparentBlock,world::blocks::Leaves,world::blocks::Water),"Water edit rejected"); wait(transparent);
            transparent.request({30,6,30}); wait(transparent);
            transparent.request(transparentCenter); scene=wait(transparent);
            require(transparent.world().getBlock(transparentBlock)==world::blocks::Water && !scene->transparency.nodes.empty(),"Eviction lost transparent geometry");
            transparent.close();
        }
        {
            streaming::ChunkStreamer transparent(123,root,0);
            transparent.request(transparentCenter);
            require(!wait(transparent)->transparency.nodes.empty(),"Restart lost transparent render metadata");
            require(transparent.world().getBlock(transparentBlock)==world::blocks::Water,"Restart lost water ID");
            require(transparent.edit(transparentBlock,world::blocks::Water,world::Air),"Water removal rejected");
            require(wait(transparent)->transparency.nodes.empty(),"Removed water retained draw ranges");
            transparent.close();
        }
        // Shutdown wakes sleeping workers and cancels in-flight work, including startup.
        for (int i=0;i<12;++i) {
            streaming::ChunkStreamer shortLived(42,root,0);
            if (i%2) shortLived.request({i,0,0});
            shortLived.close();
        }
        {
            const auto broken=root/"not-a-directory";
            { std::ofstream file(broken); file << "fixture"; }
            streaming::ChunkStreamer failing(1,broken,0);
            bool reported=false;
            try { failing.request({}); wait(failing); } catch (const std::exception&) { reported=true; }
            require(reported,"Worker file error not reported to main thread");
        }
        std::filesystem::remove_all(root); // Only this test's uniquely created temporary directory.
        std::cout << "Streaming bounds, cancellation, halo, edits, persistence, precision, shutdown, and failures passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << "\nFixtures: " << root << '\n'; return 1; }
}
