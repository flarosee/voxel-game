#include "world/ChunkCodec.hpp"
#include "world/Raycast.hpp"
#include <array>
#include <atomic>
#include <barrier>
#include <iostream>
#include <limits>
#include <random>
#include <thread>

using namespace voxel::world;
namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class Function> void rejects(Function&& function) {
    bool rejected = false;
    try { function(); } catch (const std::exception&) { rejected = true; }
    require(rejected, "Invalid input was accepted");
}
void equal(const BlockStorage& a, const BlockStorage& b) {
    require(a.occupied() == b.occupied(), "Occupancy mismatch");
    for (std::size_t i = 0; i < ChunkVolume; ++i) require(a.at(i) == b.at(i), "Block mismatch");
}

void coordinates() {
    for (auto value : {INT64_MIN, INT64_MIN + 1, INT64_C(-1000000000000), INT64_C(-17),
                      INT64_C(-16), INT64_C(-1), INT64_C(0), INT64_C(15), INT64_C(16),
                      INT64_C(17), INT64_C(1000000000000), INT64_MAX - 1, INT64_MAX}) {
        for (BlockCoord block : {BlockCoord{value,0,0}, BlockCoord{0,value,0}, BlockCoord{0,0,value}}) {
            const auto address = addressOf(block);
            require(blockAt(address.chunk, address.local) == block, "Signed coordinate round trip failed");
            require(indexOf(address.local) < ChunkVolume, "Local coordinate out of range");
        }
    }
    for (std::size_t i = 0; i < ChunkVolume; ++i) require(indexOf(localOf(i)) == i, "Index round trip failed");
    rejects([] { (void)indexOf({-1,0,0}); });
    rejects([] { (void)indexOf({0,16,0}); });
    rejects([] { (void)localOf(ChunkVolume); });
    rejects([] { (void)blockAt({MaxChunkCoord + 1,0,0}, {}); });
    require(addressOf({-1,-16,-17}).chunk == ChunkCoord{-1,-1,-2}, "Negative floor division failed");
}

void storage() {
    BlockStorage movedFrom(9);
    BlockStorage movedTo(std::move(movedFrom));
    require(movedTo.at(0) == 9 && movedFrom.at(0) == Air, "Moved-from storage is not a valid empty value");
    Chunk chunk;
    require(chunk.snapshot()->allocatedBytes() == 0, "Empty chunk allocated block payload");
    const auto empty = chunk.snapshot();
    chunk.set(0, 65535);
    require(empty->at(0) == Air && empty->occupied() == 0, "Snapshot changed after edit");
    require(chunk.snapshot()->bitsPerBlock() == 1 && chunk.snapshot()->payloadBytes() == 516,
            "Two-type chunk must use a one-bit palette");
    for (std::size_t i = 1; i <= 256; ++i) chunk.set(i, static_cast<BlockId>(i));
    require(chunk.snapshot()->bitsPerBlock() == 16 && chunk.snapshot()->payloadBytes() == 8192,
            "Direct fallback must be bounded to 8 KiB");
    for (std::size_t i = 0; i <= 256; ++i) chunk.set(i, Air);
    require(chunk.snapshot()->allocatedBytes() == 0, "Removing last block must release payload");
    {
        Chunk weakOwner(7);
        std::weak_ptr<const BlockStorage> weak = weakOwner.snapshot();
        weakOwner.set(0, 8);
        // A published version is never reused, even if no strong reader remains.
        if (auto old = weak.lock()) require(old->at(0) == 7, "Weak snapshot observed an in-place mutation");
    }

    Chunk filled(12);
    require(filled.snapshot()->occupied() == ChunkVolume && filled.snapshot()->allocatedBytes() == 0,
            "Uniform solid chunk must have no payload");
    filled.set(7, 13);
    filled.set(7, 12);
    filled.compact();
    require(filled.snapshot()->bitsPerBlock() == 0, "Uniform compaction failed");

    // Deterministic model-based test through palette growth, direct mode, and compaction.
    std::mt19937 random(98231);
    std::array<BlockId, ChunkVolume> reference{};
    for (unsigned operation = 0; operation < 12000; ++operation) {
        const auto index = static_cast<std::size_t>(random() % ChunkVolume);
        const auto id = static_cast<BlockId>(random() % 600);
        require(chunk.set(index, id) == (reference[index] != id), "Edit change flag incorrect");
        reference[index] = id;
        if (operation % 200 == 0) {
            const auto snapshot = chunk.snapshot();
            std::uint32_t count = 0;
            for (std::size_t i = 0; i < ChunkVolume; ++i) {
                require(snapshot->at(i) == reference[i], "Packed storage differs from reference");
                if (reference[i] != Air) ++count;
            }
            require(snapshot->occupied() == count, "Occupied count incorrect");
            require(snapshot->allocatedBytes() <= 8192, "Storage exceeded payload bound");
        }
    }
    const auto before = chunk.snapshot();
    rejects([&] { chunk.set(ChunkVolume, 99); });
    equal(*before, *chunk.snapshot());
    // Churn unused IDs, then explicitly compact; retained snapshots remain readable.
    Chunk churn;
    for (unsigned id = 1; id < 2000; ++id) churn.set(0, static_cast<BlockId>(id));
    churn.compact();
    require(churn.snapshot()->bitsPerBlock() == 1, "Unused palette entries not reclaimed");
}

void worldLifetime() {
    World world;
    require(!world.setBlock({100,100,100}, Air) && world.chunkCount() == 0, "Air write allocated missing chunk");
    require(world.allocateChunk({0,0,0}) && !world.allocateChunk({0,0,0}), "Duplicate allocation");
    require(world.setBlock({-1,16,-17}, 5), "Place block failed");
    const auto coordinate = addressOf({-1,16,-17}).chunk;
    const auto held = *world.snapshot(coordinate);
    require(world.compareExchangeBlock({-1,16,-17}, 5, 6), "Compare/exchange failed");
    require(!world.compareExchangeBlock({-1,16,-17}, 5, 7), "Stale edit overwrote new data");
    world.eraseChunk(coordinate);
    require(held.blocks->at(addressOf({-1,16,-17}).local) == 5, "Unload invalidated a snapshot");
    require(world.getBlock({-1,16,-17}) == Air, "Unloaded chunk still visible");
    for (auto value : {INT64_MIN, INT64_MAX}) {
        world.setBlock({value,value,value}, 65535);
        require(world.getBlock({value,value,value}) == 65535, "Extreme world coordinate failed");
    }
    require(world.compareExchangeBlock({777,0,0}, Air, 9), "CAS failed to allocate missing chunk");
    require(!world.compareExchangeBlock({888,0,0}, 2, 3), "CAS created a block with wrong expected value");
    require(!world.insertChunk({0,0,0}, BlockStorage(8)), "Import overwrote without permission");
    require(world.insertChunk({0,0,0}, BlockStorage(8), true), "Explicit replacement failed");
    require(world.getBlock({0,0,0}) == 8, "Imported data absent");
}

void fixCrc(std::vector<std::uint8_t>& bytes) {
    std::uint32_t crc = UINT32_MAX;
    for (std::size_t i = 0; i < bytes.size() - 4; ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ ((crc & 1U) ? 0xedb88320U : 0U);
    }
    crc = ~crc;
    for (unsigned i = 0; i < 4; ++i) bytes[bytes.size() - 4 + i] = static_cast<std::uint8_t>(crc >> (i * 8));
}

void serialization() {
    Chunk empty;
    const auto bytes = encodeChunk({{-1, MinChunkCoord, MaxChunkCoord}, empty.snapshot()});
    require(bytes.size() == 44 && bytes[4] == 1 && bytes[6] == 16 && bytes[8] == 255 &&
            bytes[32] == 4 && bytes[38] == 0 && bytes[39] == 16, "Little-endian uniform encoding incorrect");
    auto decoded = decodeChunk(bytes);
    require(decoded.coordinate == ChunkCoord{-1, MinChunkCoord, MaxChunkCoord}, "Saved coordinate changed");
    equal(*empty.snapshot(), decoded.blocks);
    for (std::size_t size = 0; size < bytes.size(); ++size)
        rejects([&] { (void)decodeChunk(std::span(bytes).first(size)); });
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        auto corrupt = bytes;
        corrupt[i] ^= 0x80;
        rejects([&] { (void)decodeChunk(corrupt); });
    }
    for (std::size_t offset : {std::size_t(4), std::size_t(6), std::size_t(32), std::size_t(39)}) {
        auto corrupt = bytes;
        corrupt[offset] = 0;
        fixCrc(corrupt);
        rejects([&] { (void)decodeChunk(corrupt); });
    }
    auto appended = bytes;
    appended.push_back(0);
    rejects([&] { (void)decodeChunk(appended); });
    rejects([&] { (void)encodeChunk({{}, nullptr}); });
    for (unsigned types : {1U,2U,4U,16U,256U,257U,65536U}) {
        std::array<BlockId, ChunkVolume> blocks{};
        for (std::size_t i = 0; i < ChunkVolume; ++i) blocks[i] = static_cast<BlockId>((i * 139U) % types);
        Chunk chunk(BlockStorage::fromBlocks(blocks));
        const auto original = chunk.snapshot();
        const auto encoded = encodeChunk({{1,-2,3}, original});
        const auto copy = decodeChunk(encoded);
        equal(*original, copy.blocks);
        Chunk reloaded(copy.blocks);
        require(encodeChunk({copy.coordinate, reloaded.snapshot()}) == encoded, "Save encoding not deterministic");
    }
    std::mt19937 random(729);
    for (int attempt = 0; attempt < 1000; ++attempt) {
        std::vector<std::uint8_t> garbage(random() % 200);
        for (auto& byte : garbage) byte = static_cast<std::uint8_t>(random());
        rejects([&] { (void)decodeChunk(garbage); });
    }
}

void rays() {
    World world;
    world.setBlock({-17,0,0}, 8);
    auto hit = raycast(world, {-15,0,0}, {0.5,0.5,0.5}, {-1,0,0}, 8);
    require(hit && hit->block == BlockCoord{-17,0,0} && hit->adjacent == BlockCoord{-16,0,0}, "Negative boundary ray failed");
    require(!raycast(world, {-15,0,0}, {0.5,0.5,0.5}, {-1,0,0}, 1), "Ray exceeded reach");
    world.setBlock({INT64_MAX,0,0}, 2);
    hit = raycast(world, {INT64_MAX-1,0,0}, {0.5,0.5,0.5}, {1,0,0});
    require(hit && hit->block.x == INT64_MAX, "Ray lost extreme coordinate precision");
    require(!raycast(world, {INT64_MIN,0,0}, {0,0.5,0.5}, {-1,0,0}), "Ray overflowed minimum coordinate");
    rejects([&] { (void)raycast(world, {}, {0,0,0}, {0,0,0}); });
    rejects([&] { (void)raycast(world, {}, {1,0,0}, {1,0,0}); });
}

void concurrency() {
    World world;
    world.allocateChunk({0,0,0});
    const auto original = *world.snapshot({0,0,0});
    std::barrier start(6);
    std::atomic<bool> okay{true};
    std::vector<std::jthread> threads;
    for (int writer = 0; writer < 4; ++writer) {
        threads.emplace_back([&, writer] {
            start.arrive_and_wait();
            try {
                for (int iteration = 0; iteration < 2000; ++iteration) {
                    world.setBlock({writer,0,0}, static_cast<BlockId>(iteration + 1));
                    world.setBlock({writer * 32,32,0}, static_cast<BlockId>(iteration + 1));
                }
            } catch (...) { okay = false; }
        });
    }
    threads.emplace_back([&] {
        start.arrive_and_wait();
        try {
            for (int iteration = 0; iteration < 350; ++iteration) {
                for (const auto& snapshot : world.snapshots()) {
                    const auto bytes = encodeChunk(snapshot);
                    equal(*snapshot.blocks, decodeChunk(bytes).blocks);
                    require(bytes == encodeChunk(snapshot), "Concurrent writer mutated published snapshot");
                }
                world.compactChunk({0,0,0});
            }
        } catch (...) { okay = false; }
    });
    threads.emplace_back([&] {
        start.arrive_and_wait();
        try {
            for (int iteration = 0; iteration < 3000; ++iteration) {
                world.allocateChunk({100,0,0});
                world.setBlock({1600,0,0}, 12);
                world.eraseChunk({100,0,0});
            }
        } catch (...) { okay = false; }
    });
    threads.clear(); // jthread joins; the World outlives every user.
    require(okay, "Concurrent snapshot/edit/serialization test failed");
    require(original.blocks->occupied() == 0, "Original snapshot changed under concurrency");
    for (int writer = 0; writer < 4; ++writer)
        require(world.getBlock({writer,0,0}) == 2000 && world.getBlock({writer * 32,32,0}) == 2000,
                "Lost concurrent edits");

    std::atomic<unsigned> winners{0};
    for (int i = 0; i < 12; ++i) threads.emplace_back([&, i] {
        if (world.compareExchangeBlock({-100,5,0}, Air, static_cast<BlockId>(i + 1))) ++winners;
    });
    threads.clear();
    require(winners == 1, "Compare/exchange did not have exactly one winner");
    // Same-key allocation/unload/edit races must remain safe; final value is ordered only after join.
    for (int i = 0; i < 4; ++i) threads.emplace_back([&, i] {
        try {
            for (int n = 0; n < 1500; ++n) {
                if (i == 0) world.eraseChunk({-7,0,0});
                else if (i == 1) world.allocateChunk({-7,0,0});
                else if (i == 2) world.setBlock({-112,0,0}, 3);
                else if (auto snapshot = world.snapshot({-7,0,0})) (void)encodeChunk(*snapshot);
            }
        } catch (...) { okay = false; }
    });
    threads.clear();
    require(okay, "Concurrent unload/edit failed");
    world.setBlock({-112,0,0}, 77);
    require(world.getBlock({-112,0,0}) == 77, "World unusable after lifecycle stress");
}
}

int main() {
    try {
        coordinates(); storage(); worldLifetime(); serialization(); rays(); concurrency();
        std::cout << "Chunk coordinates, storage, serialization, raycast, and concurrency checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
