# Terrain: generator version 1

The terrain layer is part of the standalone `voxel_world` library. It has no
Vulkan, GLFW, GLM, or renderer dependency. The renderer now uses
[greedy meshing](MESHING.md); terrain generation remains independent and unchanged.

## Components and seeds

- `Noise`: seeded 2D smooth value noise on signed 64-bit world coordinates.
- `Biome`: immutable interface describing surface height, material IDs, and soil depth.
- `TerrainGenerator`: pure world-coordinate sampling and chunk generation.
- `TerrainWindow`: bounded residency plus retained in-memory edits.

```powershell
.\build\windows\Release\voxel_game.exe --seed 12345
```

```bash
./build/linux/voxel_game --seed 12345
```

Seeds are unsigned 64-bit decimal integers, including zero. The default is 12345.
The seed appears in the title and console. The same **seed + generator version +
biome implementation/configuration** produces identical canonical chunk bytes.
There is no shared PRNG, clock input, `std::hash`, or generation-order dependence.

Noise hashing uses defined unsigned 64-bit arithmetic. Integer floor division
addresses the lattice; smoothstep/interpolation use fixed-point arithmetic. World
coordinates are never converted to floats during generation. Signed division uses
C++'s defined truncation toward zero. Output is in [-32768,32767]. This avoids
floating-point threshold differences and coordinate precision loss at great distances.

Version 1 uses 128-block broad elevation, 32-block detail, and 256-block moisture
scales on separate hash channels. Integer surface height is:

```text
8 + (broadNoise * 16) / 32768 + (detailNoise * 4) / 32768
```

Six fixed checksums of canonical VXC1 files guard against accidentally changing the
algorithm. Intentional changes should bump `TerrainGenerator::Version` and update
fixtures through an explicit compatibility decision. Debug, Release, and Linux CI
run the same fixtures; local Windows testing does not itself verify another OS.

## Biomes and seams

This is a heightfield: air above the surface, soil near it, stone below. There is
no fixed bottom or X/Z edge. Caves, structures, trees, and water are not included.

| Biome | Surface | Subsurface | Deep | Soil depth, including surface |
| --- | --- | --- | --- | --- |
| Grassland (moisture >= 0) | Grass, ID 1 | Dirt, ID 2 | Stone, ID 3 | 4 |
| Desert (moisture < 0) | Sand, ID 4 | Sandstone, ID 5 | Stone, ID 3 | 6 |

Both share the continuous height function, so biome boundaries do not introduce
height jumps. The mesher preserves material IDs, displayed with direct voxel skylight.

`Biome::column` is the extension point. Implementations must be immutable,
reentrant, and deterministic. The generator owns shared const references and
rejects null biomes, air material IDs, or zero soil depth. Generation returns owned
storage or throws before touching any live World.

Every column uses absolute world X/Z, and every Y is compared to its absolute
surface height. Soil continues through vertical chunk boundaries. Chunks never
reseed noise or consult loaded neighbors. Adjacent heights need not be equal;
both sides must agree with the same global terrain field.

```cpp
#include "terrain/TerrainGenerator.hpp"
#include "world/World.hpp"

const voxel::terrain::TerrainGenerator generator(12345);
auto blocks = generator.generate({-1000000, 0, 1000000});
voxel::world::World world;
world.insertChunk({-1000000, 0, 1000000}, std::move(blocks));
```

The same generator can be called concurrently. Generation supports the full signed
64-bit block space without allocating intermediate chunks. Deep/air chunks collapse
into uniform storage. The heightfield has bounded relief; the map is not preallocated.

## Streaming, edits, and lifetime

The application uses [ChunkStreamer](STREAMING.md): separate generation and meshing
workers, a 5 x 5 x 5 draw neighborhood, a neighbor halo, bounded queues, disk-backed
edits, and budgeted main-thread uploads. The generator remains pure and unchanged.

`TerrainWindow` remains a synchronous reference controller for terrain tests. Its
in-memory overrides grow with edited chunks, so it is not the application's
residency controller. Application edits go through `ChunkStreamer` commands.

## Controls and limits

- Existing movement, quaternion look, E/Q edits, and F5/F9 exports remain.
- R resets the camera above the seed's terrain.
- G regenerates the aimed chunk and resets that chunk's edits.
- F5/F9 asynchronously export/import the current world's export slot. Files are
  namespaced by seed and generator version; VXC1 remains a per-chunk format.

"Infinite" means on-demand generation across the coordinate space, not unlimited
resident memory or view distance. Camera positions now use an integer chunk origin
plus local coordinates for precise distant travel.

## Verification

The headless `terrain_generation` test covers noise bounds/smoothness, seed variation,
fixed reference checksums, material layers, both biomes, all three seam directions,
negative and INT64 coordinates, whole-volume world/chunk agreement, order independence,
concurrent generation, byte-identical regeneration, bounded streaming, retained edits,
all-air overrides, and concurrent window updates. Sanitizer CI includes these tests.

```bash
cmake -S . -B build/core -DVOXEL_BUILD_APP=OFF -DCMAKE_BUILD_TYPE=Debug
cmake --build build/core --config Debug --parallel
ctest --test-dir build/core -C Debug -R terrain_generation --output-on-failure
```

The Vulkan smoke test exercises full-radius asynchronous streaming, edits, distant
movement, resizing, and export/import. Headless tests verify residency bounds,
cancellation, persisted edits, and worker lifetime.
