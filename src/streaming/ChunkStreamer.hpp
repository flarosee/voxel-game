#pragma once

#include "../Types.hpp"
#include "mesh/GreedyMesher.hpp"
#include "mesh/Transparency.hpp"
#include "terrain/TerrainGenerator.hpp"
#include "world/World.hpp"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <exception>
#include <filesystem>
#include <mutex>
#include <thread>
#include <unordered_set>

namespace voxel::streaming {
// Fixed upload capacity, shared by CPU producer and GPU consumer.
inline constexpr std::size_t MaxSceneVertices = 1024 * 1024;
struct Scene {
    UInt64 revision = 0;
    world::ChunkCoord origin;
    float fogDistance = 0;
    std::vector<mesh::Vertex> vertices; // Relative to origin, prepared on mesh thread.
    std::uint32_t opaqueCount=0, cutoutCount=0;
    mesh::Transparency transparency;
};
struct Stats {
    std::size_t resident = 0, commands = 0, generationPending = 0, meshPending = 0, ready = 0;
    UInt64 generated = 0, meshed = 0, cancelled = 0;
};

// Owns both workers and all mutable residency. Public methods never wait for jobs.
// One controller per save directory. World access is read-only; edits go through commands.
class ChunkStreamer final {
public:
    ChunkStreamer(UInt64 seed, std::filesystem::path directory, int radius = 2);
    ~ChunkStreamer();
    ChunkStreamer(const ChunkStreamer&) = delete;
    ChunkStreamer& operator=(const ChunkStreamer&) = delete;
    UInt64 request(world::ChunkCoord center);
    bool edit(world::BlockCoord block, world::BlockId expected, world::BlockId desired);
    bool regenerate(world::ChunkCoord chunk);
    bool exportChunk(world::ChunkCoord chunk);
    bool importChunk();
    std::shared_ptr<const Scene> takeReady();
    [[nodiscard]] const world::World& world() const noexcept { return world_; }
    [[nodiscard]] Stats stats() const;
    [[nodiscard]] UInt64 revision() const noexcept { return revision_.load(); }
    // Graceful shutdown flushes edits off the main thread, joins, then reports errors.
    void close();
private:
    enum class Kind : std::uint8_t { Edit, Regenerate, Export, Import };
    struct Command { Kind kind; world::BlockCoord block{}; world::ChunkCoord chunk{}; world::BlockId expected=0, desired=0; };
    struct Request { world::ChunkCoord center; UInt64 revision; };
    struct Job { Request request; std::vector<world::ChunkSnapshot> snapshots; std::vector<world::ChunkCoord> targets; lighting::Sunlight sunlight; };
    bool enqueue(Command command);
    void generateLoop() noexcept;
    void meshLoop() noexcept;
    void fail() noexcept;
    bool stale(UInt64 revision) const noexcept;
    void apply(const Command& command);
    void save(const world::ChunkSnapshot& snapshot);
    world::BlockStorage load(world::ChunkCoord coordinate);
    std::optional<lighting::Sunlight> buildSunlight(std::span<const world::ChunkSnapshot> snapshots, UInt64 revision);
    std::filesystem::path path(world::ChunkCoord coordinate) const;
    const terrain::TerrainGenerator generator_;
    const std::filesystem::path directory_;
    const int radius_;
    world::World world_;
    std::optional<lighting::Sunlight> sunlightCache_; // Generation thread only.
    std::unordered_set<world::ChunkCoord, world::ChunkCoordHash> dirty_; // Generation thread only.
    mutable std::mutex mutex_;
    std::condition_variable generationCV_, meshCV_;
    std::optional<Request> requested_;
    std::unique_ptr<Job> meshJob_;
    std::shared_ptr<const Scene> ready_;
    std::deque<Command> commands_;
    std::exception_ptr error_;
    Stats counts_;
    std::atomic<UInt64> revision_{0};
    std::atomic<bool> stopping_{false};
    std::thread generationThread_, meshThread_;
};
} // namespace voxel::streaming
