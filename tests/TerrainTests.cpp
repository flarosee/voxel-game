#include "terrain/TerrainWindow.hpp"
#include "world/ChunkCodec.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <iostream>
#include <random>
#include <thread>

using namespace voxel;
namespace {
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
template<class F> void rejects(F&& f) {
    bool rejected = false;
    try { f(); } catch (const std::exception&) { rejected = true; }
    require(rejected, "Invalid input accepted");
}
std::uint64_t fingerprint(const terrain::TerrainGenerator& generator, world::ChunkCoord coordinate) {
    world::Chunk chunk(generator.generate(coordinate));
    auto bytes = world::encodeChunk({coordinate, chunk.snapshot()});
    std::uint64_t hash = UINT64_C(14695981039346656037);
    for (auto byte : bytes) { hash ^= byte; hash *= UINT64_C(1099511628211); }
    return hash;
}
void noise() {
    const terrain::Noise noise(12345), copy(12345), other(54321);
    unsigned differences = 0;
    for (auto base : {INT64_MIN, INT64_C(-1000000000000), INT64_C(-129), INT64_C(-1),
                     INT64_C(0), INT64_C(127), INT64_C(1000000000000), INT64_MAX-512}) {
        for (std::int64_t i = 0; i < 128; ++i) {
            const auto a = noise.sample2D(base+i,base+i,7,1);
            require(a >= -32768 && a <= 32767, "Noise escaped bounds");
            require(a == copy.sample2D(base+i,base+i,7,1), "Noise depends on instance state");
            if (a != other.sample2D(base+i,base+i,7,1)) ++differences;
            const auto next = noise.sample2D(base+i+1,base+i,7,1);
            require(std::abs(a-next) < 1000, "Noise discontinuity at lattice boundary");
        }
    }
    require(differences > 800, "Seed has insufficient effect on noise");
    (void)noise.sample2D(INT64_MAX,INT64_MIN,1,UINT64_MAX);
    rejects([&] { (void)noise.sample2D(0,0,0); });
    rejects([&] { (void)noise.sample2D(0,0,31); });
}
class FlatBiome final : public terrain::Biome {
public:
    explicit FlatBiome(std::int64_t height) : height_(height) {}
    std::string_view name() const noexcept override { return "Test flat"; }
    terrain::Column column(const terrain::TerrainSample&) const override { return {height_,11,12,13,3}; }
private:
    std::int64_t height_;
};
void layersAndBiomes() {
    auto flat = std::make_shared<FlatBiome>(0);
    terrain::TerrainGenerator generator(9,flat,flat);
    require(generator.sampleBlock({-1,1,16}) == world::Air, "Air above terrain missing");
    require(generator.sampleBlock({-1,0,16}) == 11, "Surface ID incorrect");
    require(generator.sampleBlock({-1,-1,16}) == 12 && generator.sampleBlock({-1,-2,16}) == 12, "Soil layers incorrect");
    require(generator.sampleBlock({-1,-3,16}) == 13, "Deep material incorrect");
    auto extreme = std::make_shared<FlatBiome>(INT64_MAX);
    terrain::TerrainGenerator high(0,extreme,extreme);
    require(high.sampleBlock({INT64_MIN,INT64_MIN,INT64_MAX}) == 13, "Height distance overflow");
    require(high.sampleBlock({0,INT64_MAX,0}) == 11, "Extreme surface incorrect");
    terrain::TerrainGenerator defaults;
    bool grass = false, desert = false;
    for (std::int64_t x = -1024; x <= 1024; x += 64) {
        const auto column = defaults.sampleColumn(x,317);
        grass |= column.surface == terrain::blocks::Grass;
        desert |= column.surface == terrain::blocks::Sand;
        require(defaults.sampleBlock({x,INT64_MIN,317}) == terrain::blocks::Stone, "Terrain has a bottom limit");
        require(defaults.sampleBlock({x,INT64_MAX,317}) == world::Air, "Terrain has a top-coordinate limit");
    }
    require(grass && desert, "Both default biomes must be reachable");
    rejects([] { terrain::TerrainGenerator invalid(1,nullptr,nullptr); });
}
void seams() {
    terrain::TerrainGenerator generator;
    for (world::ChunkCoord coordinate : {world::ChunkCoord{0,0,0}, {-1,-1,-1},
         {1000000000,0,-1000000000}, {world::MinChunkCoord,0,world::MinChunkCoord},
         {world::MaxChunkCoord-1,0,world::MaxChunkCoord-1}}) {
        const auto a = generator.generate(coordinate);
        for (int axis = 0; axis < 3; ++axis) {
            auto neighbor = coordinate;
            if (axis == 0) ++neighbor.x;
            if (axis == 1) ++neighbor.y;
            if (axis == 2) ++neighbor.z;
            const auto b = generator.generate(neighbor);
            for (int u = 0; u < 16; ++u) for (int v = 0; v < 16; ++v) {
                const world::LocalCoord left = axis == 0 ? world::LocalCoord{15,u,v} :
                    axis == 1 ? world::LocalCoord{u,15,v} : world::LocalCoord{u,v,15};
                auto right = left;
                if (axis == 0) right.x = 0;
                if (axis == 1) right.y = 0;
                if (axis == 2) right.z = 0;
                const auto p = world::blockAt(coordinate,left), q = world::blockAt(neighbor,right);
                require(a.at(left) == generator.sampleBlock(p) && b.at(right) == generator.sampleBlock(q),
                        "Chunk border differs from global terrain");
                if (axis != 1) require(std::abs(generator.sampleColumn(p.x,p.z).surfaceHeight -
                    generator.sampleColumn(q.x,q.z).surfaceHeight) <= 2, "Height jump across seam");
            }
        }
    }
    // Compare whole adjacent volumes against an independent world-coordinate traversal.
    for (int cx = -1; cx <= 0; ++cx) for (int cy = -1; cy <= 1; ++cy) {
        const auto chunk = generator.generate({cx,cy,-1});
        for (int z = -16; z < 0; ++z) for (int y = cy*16; y < cy*16+16; ++y)
            for (int x = cx*16; x < cx*16+16; ++x)
                require(chunk.at({x-cx*16,y-cy*16,z+16}) == generator.sampleBlock({x,y,z}),
                        "Chunk-local generation disagrees with full-volume sampling");
    }
    rejects([&] { (void)generator.generate({world::MaxChunkCoord+1,0,0}); });
}
void repeatability() {
    const terrain::TerrainGenerator generator(12345), copy(12345), other(987654321);
    std::array<world::ChunkCoord,6> coordinates{{{0,0,0},{-1,0,-1},{134,0,-89},
        {world::MaxChunkCoord,0,world::MinChunkCoord},{-17,-1,31},{1,1,1}}};
    std::array<std::uint64_t,6> hashes{};
    // Version-1 fixtures over canonical VXC1 bytes: detect accidental algorithm
    // changes across builds/platforms, not just agreement between two copies.
    constexpr std::array<std::uint64_t,6> golden{
        UINT64_C(15494154955659514390), UINT64_C(9532639039750545419),
        UINT64_C(3840499093166439469), UINT64_C(16543963336631246398),
        UINT64_C(5633799164826125258), UINT64_C(2057164068228428143)};
    unsigned differences = 0;
    for (std::size_t i = 0; i < coordinates.size(); ++i) {
        hashes[i] = fingerprint(generator,coordinates[i]);
        require(hashes[i] == fingerprint(copy,coordinates[i]), "Same seed regenerated different bytes");
        differences += hashes[i] != fingerprint(other,coordinates[i]) ? 1U : 0U;
        require(hashes[i] == golden[i], "Generator version-1 fixture changed");
    }
    require(differences >= 3, "Different seeds generated identical terrain");
    for (std::size_t i = coordinates.size(); i-- > 0;)
        require(hashes[i] == fingerprint(generator,coordinates[i]), "Generation order affected output");
    std::atomic<bool> okay{true};
    std::vector<std::jthread> jobs;
    for (int worker = 0; worker < 6; ++worker) jobs.emplace_back([&,worker] {
        try {
            for (unsigned repeat = 0; repeat < 12; ++repeat) {
                const auto i = (static_cast<std::size_t>(worker) + repeat) % coordinates.size();
                if (fingerprint(generator,coordinates[i]) != hashes[i]) okay = false;
            }
        } catch (...) { okay = false; }
    });
    jobs.clear();
    require(okay, "Concurrent generation changed output");
    for (const std::uint64_t seed : {UINT64_C(0), UINT64_C(1), UINT64_MAX}) {
        const terrain::TerrainGenerator a(seed), b(seed);
        require(fingerprint(a,{-3,0,5}) == fingerprint(b,{-3,0,5}), "Extreme seed failed regeneration");
    }
}
void streaming() {
    world::World world;
    terrain::TerrainGenerator generator;
    terrain::TerrainWindow window(world,generator,1);
    window.update({0,0,0});
    require(world.chunkCount() == 27, "Neighborhood size incorrect");
    const auto held = *world.snapshot({0,0,0});
    const auto bytes = world::encodeChunk(held);
    window.update({100,0,100});
    require(!world.snapshot({0,0,0}) && world.chunkCount() == 27 && window.retainedEdits() == 0,
            "Unedited terrain was not evicted with bounded memory");
    require(world::encodeChunk(held) == bytes, "Streaming invalidated snapshot");
    window.update({0,0,0});
    require(world::encodeChunk(*world.snapshot({0,0,0})) == bytes, "Unload/regenerate changed chunk bytes");
    world.setBlock({1,1,1},65535);
    window.update({100,0,100});
    require(window.retainedEdits() == 1, "Edited chunk not retained");
    window.update({0,0,0});
    require(world.getBlock({1,1,1}) == 65535, "Streaming lost an edit");
    world.setBlock({1,1,1},generator.sampleBlock({1,1,1}));
    window.update({100,0,100});
    require(window.retainedEdits() == 0, "Restored-to-baseline chunk retained unnecessarily");
    window.update({world::MaxChunkCoord,world::MaxChunkCoord,world::MinChunkCoord});
    require(world.chunkCount() == 8, "World-limit neighborhood overflowed");
    window.update({world::MinChunkCoord,world::MinChunkCoord,world::MaxChunkCoord});
    require(world.chunkCount() == 8, "Negative world-limit neighborhood overflowed");
    window.update({0,0,0});
    world.insertChunk({0,0,0},world::BlockStorage(world::Air),true);
    window.update({10,0,10});
    window.update({0,0,0});
    require(world.snapshot({0,0,0})->blocks->occupied() == 0, "An edited-to-empty chunk regenerated solids");
    // Calls to the same residency controller are serialized, including its edit cache.
    std::atomic<bool> okay{true};
    std::vector<std::jthread> updates;
    for (int worker = 0; worker < 2; ++worker) updates.emplace_back([&,worker] {
        try {
            for (int i = 0; i < 8; ++i) window.update({worker*20+i,0,worker*20-i});
        } catch (...) { okay = false; }
    });
    updates.clear();
    require(okay, "Concurrent window updates failed");
    window.update({0,0,0});
    require(world.chunkCount() == 27 && world.snapshot({0,0,0})->blocks->occupied() == 0,
            "Concurrent streaming lost residency or retained edits");
}
}
int main() {
    try {
        noise(); layersAndBiomes(); seams(); repeatability(); streaming();
        std::cout << "Noise, biome, seams, seed, regeneration, concurrency, and streaming checks passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
