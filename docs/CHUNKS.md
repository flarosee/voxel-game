# Chunk storage and concurrency contract

`voxel_world` is a standalone C++20 library. It depends only on the standard
library and threads, with no Vulkan, GLFW, GLM, or graphics-thread state.

## Coordinates and allocation

- Chunks are 16 x 16 x 16, with 4,096 cells. X, Y, and Z are treated identically.
- Block coordinates are signed 64-bit integers: -2^63 through 2^63-1 on every axis.
  There is no column allocation, fixed world height, or fixed world depth.
- Chunk coordinates range from -2^59 through 2^59-1, covering that entire block
  space. Allocation rejects chunk coordinates outside that range.
- Negative coordinates use floor division: block -1 belongs to chunk -1, local 15.
  Multiplication and reconstruction are checked before arithmetic can overflow.
- Blocks occupy [x,x+1] x [y,y+1] x [z,z+1]. Local order is
  `x + 16 * (y + 16 * z)`; X varies fastest.
- A sparse hash map contains only allocated chunks. Reads of missing chunks return
  air without allocating. Non-air edits allocate on demand. Removing from missing
  space is a no-op. Empty allocated chunks stay allocated until explicitly erased.
- IDs are unsigned 16-bit values. ID 0 is air; 1..65535 are available to a future
  block registry. Blocks contain no per-cell pointers, positions, light, or objects.

## Memory representation

| Representation | Block payload (not object/map/allocator overhead) |
| --- | --- |
| Uniform air or uniform solid | 0 heap bytes; one ID in the storage object |
| 2 palette entries | 512 index bytes + 4 palette bytes |
| Up to 4 entries | 1,024 index bytes + up to 8 palette bytes |
| Up to 16 entries | 2,048 index bytes + up to 32 palette bytes |
| Up to 256 entries | 4,096 index bytes + up to 512 palette bytes |
| More than 256 distinct IDs | 8,192 bytes of direct IDs; no palette |

Packed widths are 1, 2, 4, 8, or 16 bits, so an entry never crosses a 64-bit word.
`payloadBytes()` reports used bytes, and `allocatedBytes()` includes vector spare
capacity. Neither includes the chunk lock, storage object, shared ownership control
block, map node/buckets, or allocator bookkeeping. Allocation failures propagate as
exceptions; edits finish allocations before committing, leaving the old data intact.

Deleting the last solid cell automatically releases the payload. `compactChunk()`
reclaims unused palette IDs, reduces bit width, and collapses uniform chunks after
other edits. Before promoting a full 256-entry palette, storage first rebuilds it
to reclaim unused IDs. Compaction is explicit because scanning 4,096 cells after
every ordinary edit would be unnecessary work. Storage itself never generates terrain;
see [the separate terrain layer](TERRAIN.md).

## Thread safety and lifetime

Every `World` operation is safe to call concurrently while that `World` remains
alive. The caller must stop/join workers before destroying the World. This is a
locking implementation, not lock-free, and does not promise fairness or real-time
latency from the platform's `std::shared_mutex`.

The lock order is always **world map, then chunk**. Existing chunks can be edited
concurrently using a shared map lock and separate exclusive chunk locks. The map
lock stays held until an edit completes, so unloading cannot detach a chunk in the
middle of a successful edit. Allocation, unload, and imported replacement take an
exclusive map lock. No callbacks or file I/O run under these locks.

Snapshots expose `shared_ptr<const BlockStorage>`. Taking a snapshot briefly takes
the chunk's exclusive lock to mark that storage version as published. Published
storage is never mutated again. The next edit clones it; subsequent edits may
modify that new version in place until it too is published. This remains safe even
if a reader keeps only a weak pointer and locks it later. A reference-count check
alone would not provide that guarantee.

A snapshot remains readable through later edits, compaction, chunk unload,
replacement, and World destruction. Holding old snapshots deliberately retains
their memory; release them when a worker/render job is finished. Never cast away
constness or use raw references after releasing their owning snapshot.

Individual reads/edits and compare/exchange operations are atomic with respect to
other World operations on the same block. **Multiple calls are not a transaction.**
`snapshots()` stabilizes map membership during capture, but different chunks can
represent different instants. Raycasts also read individual cells and may observe
concurrent changes. The demo uses `compareExchangeBlock` for edits to avoid replacing
a currently different ID. It is not a revision check: an ID changed away and back
again (ABA) still matches. Multi-chunk transactions/revisioned jobs are future work.

`insertChunk` prepares storage before acquiring the map lock. Its default refuses
an existing coordinate. Passing `replace=true` explicitly authorizes replacing the
entire chunk in lock order, including edits that completed before replacement.

The meshing worker consumes snapshots and holds no world locks while building geometry.
GLFW, the camera, and Vulkan remain confined to the main thread. The application now
uses a generation worker and meshing worker; see [streaming](STREAMING.md).

## Serialization format (VXC1, version 1)

`encodeChunk(snapshot)` returns owned bytes. `decodeChunk(bytes)` returns a fully
validated coordinate and storage value without touching a live world. Both can
run on a worker, provided the caller does not concurrently mutate the input byte
buffer. All integers are explicitly little-endian; no structs, padding, pointers,
mutexes, native-endian arrays, or in-memory palette indices are written.

| Offset | Bytes | Field |
| --- | --- | --- |
| 0 | 4 | ASCII `VXC1` |
| 4 | 2 | Version, 1 |
| 6 | 2 | Chunk side, 16 |
| 8 | 24 | Signed two's-complement int64 chunk X, Y, Z |
| 32 | 4 | Payload byte length |
| 36 | Variable | RLE records: uint16 block ID, uint16 run length |
| End - 4 | 4 | CRC-32/ISO-HDLC over all preceding bytes |

Runs traverse X-fastest storage order and must expand to exactly 4,096 cells. Zero
runs, excessive runs, adjacent equal-ID runs, invalid dimensions/coordinates,
unsupported versions, truncated/trailing bytes, and checksum failures are rejected.
The decoder enforces a 16,424-byte maximum before decoding. Uniform chunks occupy
44 bytes on disk; incompressible chunks can occupy 16,424 bytes. CRC detects
accidental corruption, not malicious authenticity. Memory and file representations
are intentionally independent so the storage layout can evolve.

The streaming controller writes edited chunks on eviction and orderly shutdown
under a seed/version directory. F5 asynchronously replaces that world's export
slot; F9 reloads it when its chunk is resident. See [streaming](STREAMING.md) for
replacement, recovery, and durability limits. VXC1 remains the same bounded format.

## API example

```cpp
#include "world/World.hpp"
#include "world/ChunkCodec.hpp"

voxel::world::World world;
world.allocateChunk({0, -1000000, 0}); // Uniform air, no block array.
world.setBlock({-1, -16000000, 0}, 42); // Automatically allocates neighboring chunk.
auto snapshot = world.snapshot({-1, -1000000, 0}).value();
auto bytes = voxel::world::encodeChunk(snapshot); // Safe after unload / later edits.
world.setBlock({-1, -16000000, 0}, voxel::world::Air);
auto decoded = voxel::world::decodeChunk(bytes);
world.insertChunk(decoded.coordinate, std::move(decoded.blocks), true);
```

## Rendering scope

The renderer now builds greedy meshes from immutable snapshots. See
[meshing](MESHING.md) for neighbors, UVs, normals, crack prevention, and rebuilds.

Storage and integer-origin raycasts support signed 64-bit block coordinates. The
camera now uses an integer chunk origin and local floats; meshes use nearby
relative coordinates. This preserves precision during distant travel. Residency
and GPU upload buffers have fixed bounds; view distance remains finite.

## Standalone verification

The core builds without network downloads, graphics libraries, a window, or a GPU:

```bash
cmake -S . -B build/core -DVOXEL_BUILD_APP=OFF -DCMAKE_BUILD_TYPE=Debug
cmake --build build/core --config Debug --parallel
ctest --test-dir build/core -C Debug --output-on-failure
```

On Linux with GCC/Clang, use separate directories for memory/undefined-behavior
checks and data-race checks:

```bash
cmake -S . -B build/core-asan -DVOXEL_BUILD_APP=OFF -DVOXEL_SANITIZER=address -DCMAKE_BUILD_TYPE=Debug
cmake --build build/core-asan --parallel
ctest --test-dir build/core-asan --output-on-failure

cmake -S . -B build/core-tsan -DVOXEL_BUILD_APP=OFF -DVOXEL_SANITIZER=thread -DCMAKE_BUILD_TYPE=Debug
cmake --build build/core-tsan --parallel
ctest --test-dir build/core-tsan --output-on-failure
```

Tests cover signed-coordinate extremes and negative boundaries; all palette widths;
12,000 deterministic edits against a flat reference array; empty/uniform compaction;
retained and weak snapshot lifetimes; bounded/corrupt/truncated saves; deterministic
round trips; boundary raycasts; concurrent same/different-chunk edits, snapshotting,
serialization, compaction, allocation/unload races, and compare/exchange contention.
Sanitizer CI is configured separately. Tests provide evidence, not a proof of the
absence of every race or failure under every possible schedule.
