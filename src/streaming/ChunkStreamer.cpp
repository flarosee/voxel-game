#include "ChunkStreamer.hpp"
#include "world/BlockTypes.hpp"
#include "world/ChunkCodec.hpp"
#include <fstream>
#include <algorithm>
#include <utility>
#include <charconv>

namespace voxel::streaming {
namespace {
using namespace world;
std::optional<ChunkCoord> chunkFilename(std::string name) {
    if (name.ends_with(".bak")) name.resize(name.size()-4);
    if (!name.ends_with(".vxc")) return std::nullopt;
    name.resize(name.size()-4);
    ChunkCoord coordinate;
    std::int64_t* fields[]{&coordinate.x,&coordinate.y,&coordinate.z};
    const char* cursor=name.data();
    const char* end=cursor+name.size();
    for (int i=0;i<3;++i) {
        const auto [next,error]=std::from_chars(cursor,end,*fields[i]);
        if (error!=std::errc{} || (i<2 ? next==end || *next!='_' : next!=end)) return std::nullopt;
        cursor=i<2 ? next+1 : next;
    }
    world::validate(coordinate);
    return coordinate;
}
std::vector<ChunkCoord> neighborhood(ChunkCoord center, int radius) {
    std::vector<ChunkCoord> result;
    // Center first, then shells; bound checked before converting to block coordinates.
    for (int shell=0; shell<=radius; ++shell)
        for (int z=-shell; z<=shell; ++z) for (int y=-shell; y<=shell; ++y) for (int x=-shell; x<=shell; ++x) {
            if (std::max({std::abs(x),std::abs(y),std::abs(z)}) != shell) continue;
            ChunkCoord c{center.x+x,center.y+y,center.z+z};
            if (c.x<MinChunkCoord || c.x>MaxChunkCoord || c.y<MinChunkCoord || c.y>MaxChunkCoord || c.z<MinChunkCoord || c.z>MaxChunkCoord) continue;
            result.push_back(c);
        }
    return result;
}
DecodedChunk read(const std::filesystem::path& file) {
    std::ifstream input(file, std::ios::binary | std::ios::ate);
    input.exceptions(std::ios::badbit | std::ios::failbit);
    const auto size=input.tellg();
    if (size<0 || size>static_cast<std::streamoff>(MaxChunkFileBytes)) throw std::runtime_error("Invalid streamed chunk size");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    input.seekg(0);
    input.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
    return decodeChunk(bytes);
}
void write(const std::filesystem::path& file, const ChunkSnapshot& snapshot) {
    const auto bytes=encodeChunk(snapshot);
    auto temp=file; temp += ".tmp";
    auto backup=file; backup += ".bak";
    { std::ofstream out(temp,std::ios::binary|std::ios::trunc);
      out.exceptions(std::ios::badbit|std::ios::failbit);
      out.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
      out.close(); }
    // Keep a recoverable previous copy across the portable two-rename replacement.
    if (std::filesystem::exists(file)) {
        std::filesystem::remove(backup);
        std::filesystem::rename(file,backup);
    }
    std::filesystem::rename(temp,file);
    std::filesystem::remove(backup);
}
}
ChunkStreamer::ChunkStreamer(std::uint64_t seed, std::filesystem::path directory, int radius)
    : generator_(seed), directory_(std::move(directory)/ ("v1-seed-"+std::to_string(seed))), radius_(radius) {
    if (radius<0 || radius>2) throw std::invalid_argument("Streaming radius must be within [0,2]");
    generationThread_=std::thread([this]{generateLoop();});
    try { meshThread_=std::thread([this]{meshLoop();}); }
    catch (...) { { std::lock_guard lock(mutex_); stopping_=true; } generationCV_.notify_all(); generationThread_.join(); throw; }
}
ChunkStreamer::~ChunkStreamer() { try { close(); } catch (...) {} }
void ChunkStreamer::close() {
    { std::lock_guard lock(mutex_); stopping_=true; }
    generationCV_.notify_all(); meshCV_.notify_all();
    if (generationThread_.joinable()) generationThread_.join();
    if (meshThread_.joinable()) meshThread_.join();
    std::lock_guard lock(mutex_);
    if (error_) std::rethrow_exception(error_);
}
std::uint64_t ChunkStreamer::request(world::ChunkCoord center) {
    world::validate(center);
    std::lock_guard lock(mutex_);
    if (error_) std::rethrow_exception(error_);
    if (stopping_) throw std::runtime_error("Streamer stopped");
    if (requested_ && requested_->center==center) return requested_->revision;
    requested_=Request{center,++revision_};
    generationCV_.notify_one();
    return requested_->revision;
}
bool ChunkStreamer::enqueue(Command command) {
    std::lock_guard lock(mutex_);
    if (stopping_ || !requested_ || commands_.size()>=32) return false;
    commands_.push_back(command);
    requested_->revision=++revision_;
    generationCV_.notify_one();
    return true;
}
bool ChunkStreamer::edit(world::BlockCoord b,world::BlockId expected,world::BlockId desired) { return enqueue({Kind::Edit,b,{},expected,desired}); }
bool ChunkStreamer::regenerate(world::ChunkCoord c) { world::validate(c); return enqueue({Kind::Regenerate,{},c}); }
bool ChunkStreamer::exportChunk(world::ChunkCoord c) { world::validate(c); return enqueue({Kind::Export,{},c}); }
bool ChunkStreamer::importChunk() { return enqueue({Kind::Import}); }
std::shared_ptr<const Scene> ChunkStreamer::takeReady() {
    std::lock_guard lock(mutex_);
    if (error_) std::rethrow_exception(error_);
    if (ready_ && ready_->revision != revision_) ready_.reset();
    return std::exchange(ready_,{});
}
Stats ChunkStreamer::stats() const {
    std::lock_guard lock(mutex_);
    auto result=counts_;
    result.resident=world_.chunkCount(); result.commands=commands_.size();
    result.generationPending=requested_ && requested_->revision!=counts_.generated;
    result.meshPending=meshJob_!=nullptr; result.ready=ready_!=nullptr;
    return result;
}
bool ChunkStreamer::stale(std::uint64_t revision) const noexcept { return stopping_ || revision!=revision_; }
void ChunkStreamer::fail() noexcept {
    { std::lock_guard lock(mutex_); if (!error_) error_=std::current_exception(); stopping_=true; }
    generationCV_.notify_all(); meshCV_.notify_all();
}
std::filesystem::path ChunkStreamer::path(world::ChunkCoord c) const {
    return directory_/(std::to_string(c.x)+"_"+std::to_string(c.y)+"_"+std::to_string(c.z)+".vxc");
}
void ChunkStreamer::save(const world::ChunkSnapshot& snapshot) { write(path(snapshot.coordinate),snapshot); }
world::BlockStorage ChunkStreamer::load(world::ChunkCoord c) {
    auto file=path(c);
    if (!std::filesystem::exists(file)) { auto backup=file; backup += ".bak"; if (std::filesystem::exists(backup)) file=backup; }
    if (!std::filesystem::exists(file)) return generator_.generate(c);
    auto decoded=read(file);
    if (decoded.coordinate!=c) throw std::runtime_error("Streamed chunk coordinate mismatch");
    return std::move(decoded.blocks);
}
std::optional<lighting::Sunlight> ChunkStreamer::buildSunlight(
    std::span<const world::ChunkSnapshot> snapshots, std::uint64_t revision) {
    std::unordered_map<world::ChunkCoord,world::ChunkSnapshot,world::ChunkCoordHash> resident;
    for (const auto& snapshot:snapshots) resident.emplace(snapshot.coordinate,snapshot);
    // Constant-size disk cache; removing tall columns cannot grow an in-memory index.
    using Entry=std::pair<world::ChunkCoord,std::optional<world::ChunkSnapshot>>;
    std::deque<Entry> cache;
    const auto readOverride=[&](world::ChunkCoord coordinate)->std::optional<world::ChunkSnapshot> {
        if (auto it=resident.find(coordinate);it!=resident.end()) return it->second;
        for (const auto& entry:cache) if (entry.first==coordinate) return entry.second;
        auto file=path(coordinate);
        if (!std::filesystem::exists(file)) file += ".bak";
        std::optional<world::ChunkSnapshot> result;
        if (std::filesystem::exists(file)) {
            auto decoded=read(file);
            if (decoded.coordinate!=coordinate) throw std::runtime_error("Sunlight override coordinate mismatch");
            result=world::ChunkSnapshot{coordinate,std::make_shared<const world::BlockStorage>(std::move(decoded.blocks))};
        }
        if (cache.size()==8) cache.pop_front();
        cache.emplace_back(coordinate,result);
        return result;
    };
    auto sunlight=lighting::Sunlight::terrain(generator_,snapshots,readOverride,[&]{return stale(revision);},
                                              sunlightCache_ ? &*sunlightCache_ : nullptr);
    if (!sunlight) return std::nullopt;
    // A roof can be arbitrarily high above the resident halo. Stream saved-file
    // metadata instead of treating that unloaded space as empty or keeping all
    // edited chunks in RAM. Only this footprint's overrides are decoded.
    for (const auto& entry:std::filesystem::directory_iterator(directory_)) {
        if (stale(revision)) return std::nullopt;
        if (!entry.is_regular_file()) continue;
        const auto coordinate=chunkFilename(entry.path().filename().string());
        if (!coordinate || !sunlight->contains(*coordinate) || resident.contains(*coordinate)) continue;
        if (const auto snapshot=readOverride(*coordinate)) sunlight->include(*snapshot);
    }
    if (stale(revision)) return std::nullopt;
    sunlightCache_=*sunlight;
    return sunlight;
}
void ChunkStreamer::apply(const Command& c) {
    if (c.kind==Kind::Edit) {
        const auto coordinate=world::addressOf(c.block).chunk;
        // Do not recreate an evicted chunk from a stale player command.
        if (world_.snapshot(coordinate)) {
            // Reserve persistence bookkeeping before mutating live data.
            dirty_.insert(coordinate);
            if (sunlightCache_ && world::blocks::opaque(c.expected)!=world::blocks::opaque(c.desired)) sunlightCache_->invalidate(c.block);
            world_.compareExchangeBlock(c.block,c.expected,c.desired);
        }
    } else if (c.kind==Kind::Regenerate) {
        if (world_.snapshot(c.chunk)) {
            dirty_.insert(c.chunk);
            if (sunlightCache_) sunlightCache_->invalidateChunk(c.chunk);
            world_.insertChunk(c.chunk,generator_.generate(c.chunk),true);
        }
    } else if (c.kind==Kind::Export) {
        if (const auto snapshot=world_.snapshot(c.chunk)) write(directory_/"export.vxc",*snapshot);
    } else {
        const auto file=directory_/"export.vxc";
        if (std::filesystem::exists(file)) {
            auto decoded=read(file);
            if (world_.snapshot(decoded.coordinate)) {
                dirty_.insert(decoded.coordinate);
                if (sunlightCache_) sunlightCache_->invalidateChunk(decoded.coordinate);
                world_.insertChunk(decoded.coordinate,std::move(decoded.blocks),true);
            }
        }
    }
}
void ChunkStreamer::generateLoop() noexcept {
    try {
        std::filesystem::create_directories(directory_);
        std::uint64_t completed=0;
        while (!stopping_) {
            Request request;
            std::deque<Command> commands;
            {
                std::unique_lock lock(mutex_);
                generationCV_.wait(lock,[&]{return stopping_ || (requested_ && requested_->revision!=completed);});
                if (stopping_) break;
                request=*requested_; commands.swap(commands_);
            }
            for (const auto& command:commands) apply(command);
            const auto wanted=neighborhood(request.center,radius_+1);
            const std::unordered_set<world::ChunkCoord,world::ChunkCoordHash> membership(wanted.begin(),wanted.end());
            for (const auto& snapshot:world_.snapshots()) if (!membership.contains(snapshot.coordinate)) {
                if (dirty_.contains(snapshot.coordinate)) { save(snapshot); dirty_.erase(snapshot.coordinate); }
                world_.eraseChunk(snapshot.coordinate);
            }
            for (const auto coordinate:wanted) {
                if (stale(request.revision)) break;
                if (!world_.snapshot(coordinate)) world_.insertChunk(coordinate,load(coordinate));
            }
            if (stale(request.revision)) { std::lock_guard lock(mutex_); ++counts_.cancelled; continue; }
            auto snapshots=world_.snapshots();
            auto sunlight=buildSunlight(snapshots,request.revision);
            if (!sunlight) { std::lock_guard lock(mutex_); ++counts_.cancelled; continue; }
            auto job=std::make_unique<Job>(Job{request,std::move(snapshots),neighborhood(request.center,radius_),std::move(*sunlight)});
            {
                std::lock_guard lock(mutex_);
                if (!stale(request.revision)) { meshJob_=std::move(job); completed=request.revision; counts_.generated=completed; }
            }
            meshCV_.notify_one();
        }
        // Honor accepted edits before orderly shutdown; no main-thread file I/O.
        std::deque<Command> remaining;
        { std::lock_guard lock(mutex_); remaining.swap(commands_); }
        for (const auto& command:remaining) apply(command);
        for (const auto coordinate:dirty_) if (auto snapshot=world_.snapshot(coordinate)) save(*snapshot);
    } catch (...) { fail(); }
}
void ChunkStreamer::meshLoop() noexcept {
    try {
        std::optional<lighting::BlockLight> lightCache;
        while (!stopping_) {
            std::unique_ptr<Job> job;
            { std::unique_lock lock(mutex_);
              meshCV_.wait(lock,[&]{return stopping_ || meshJob_!=nullptr;});
              if (stopping_) break;
              job=std::move(meshJob_); }
            auto scene=std::make_shared<Scene>();
            scene->revision=job->request.revision; scene->origin=job->request.center;
            scene->fogDistance=radius_==0 ? 0.0F : static_cast<float>(radius_*world::ChunkSide-4);
            auto blockLight=lightCache ? lightCache->updated(job->snapshots,[&]{return stale(scene->revision);})
                                       : lighting::BlockLight::build(job->snapshots,[&]{return stale(scene->revision);});
            if (!blockLight) { std::lock_guard lock(mutex_); ++counts_.cancelled; continue; }
            lightCache=std::move(blockLight); // Commit only a fully converged field.
            std::vector<mesh::Quad> cutouts, blended;
            std::size_t baseVertexCount=0;
            for (const auto coordinate:job->targets) {
                if (stale(scene->revision)) break;
                auto chunk=mesh::buildOne(job->snapshots,coordinate,&job->sunlight,&*lightCache);
                if (chunk.vertices.size()>MaxSceneVertices-baseVertexCount) throw std::length_error("Streaming scene exceeds fixed vertex budget");
                baseVertexCount+=chunk.vertices.size();
                const std::array offset{(coordinate.x-scene->origin.x)*world::ChunkSide,
                    (coordinate.y-scene->origin.y)*world::ChunkSide,(coordinate.z-scene->origin.z)*world::ChunkSide};
                for (auto quad:chunk.quads) {
                    for (std::size_t axis=0;axis<3;++axis) quad.origin[axis]+=static_cast<int>(offset[axis]);
                    if (world::blocks::blended(quad.material)) blended.push_back(quad);
                    else if (world::blocks::cutout(quad.material)) cutouts.push_back(quad);
                    else mesh::appendQuad(scene->vertices,quad);
                }
            }
            if (stale(scene->revision)) { std::lock_guard lock(mutex_); ++counts_.cancelled; continue; }
            scene->opaqueCount=static_cast<std::uint32_t>(scene->vertices.size());
            for (const auto& quad:cutouts) mesh::appendQuad(scene->vertices,quad);
            scene->cutoutCount=static_cast<std::uint32_t>(scene->vertices.size())-scene->opaqueCount;
            auto transparency=mesh::buildTransparency(std::move(blended),scene->vertices,MaxSceneVertices,
                [&]{return stale(scene->revision);});
            if (!transparency) { std::lock_guard lock(mutex_); ++counts_.cancelled; continue; }
            scene->transparency=std::move(*transparency);
            { std::lock_guard lock(mutex_);
              if (!stale(scene->revision)) { counts_.meshed=scene->revision; ready_=std::move(scene); }
              else ++counts_.cancelled; }
        }
    } catch (...) { fail(); }
}
} // namespace voxel::streaming
