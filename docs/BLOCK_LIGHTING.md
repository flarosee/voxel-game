# Block lighting

Block ID **6** is an opaque lamp, registered in `world/BlockTypes.hpp`. It emits
level **15**. Terrain material IDs 1–5 retain their existing meanings. Lamps use
the normal chunk codec, so placement persists through eviction and restart.
Lighting is derived from saved blocks; light arrays are not serialized.

## Rules

Block light spreads through air and transparent blocks along the six axis-aligned neighbors, losing one
level per step. The source is 15, adjacent air is 14, distance 14 receives 1, and
distance 15 receives 0. Distance follows the shortest available air path, so light
can travel around corners. Opaque cells block transmission, including
lamps; each lamp independently emits into its transmitting neighbors. Overlapping sources
use the maximum level, not an additive sum.

Faces sample adjacent air. A lamp's own visible faces also receive its emission,
including its underside. Sunlight and block light stay separate. The material
shader combines white skylight with warm block light using a per-color-channel
maximum. This preserves daylight brightness and reveals lamps in covered areas.
There is no ambient brightness floor, ambient occlusion, bloom, transparent block
absorption beyond the normal one-level attenuation, variable source strength, or colored-light propagation in this phase.

## Rebuild and lifetime

`BlockLight::build` initializes a fresh multisource breadth-first traversal on the
meshing worker using the same immutable snapshot batch as sunlight and geometry.
All sources have equal strength, so FIFO visits cells in distance order and each
air cell enters the queue at most once. Missing chunks never transmit light.
Cancellation is checked during source scanning and traversal. Independent builds
have no shared mutable state.

Subsequent batches use `BlockLight::updated` to propagate increases and decreases
from changed cells and residency boundaries. Completed fields retain immutable
input snapshots so skipped revisions can be compared directly. Cancelled updates
discard their partial result. See [light propagation updates](LIGHT_UPDATES.md).
Geometry still uses the existing full scene rebuild and budgeted upload path.

The existing one-chunk (16-block) generation halo covers the entire nonzero light
range plus adjacent-air face sampling. Therefore every source and path that can
affect a visible face is available even when the source's chunk is outside the
draw neighborhood. Geometry and both lighting channels swap together only after
a complete upload, and stale revisions remain rejected.

## Storage and meshing

Each snapshot chunk gets a packed four-bit field: **2 KiB per chunk**, at most
**686 KiB for 343 resident chunks**. The temporary FIFO contains at most one entry
per resident voxel; its capacity is bounded by the snapshot volume. Incremental
updates also use a 512-byte pending bitset per chunk and retain the previous
completed field while constructing its replacement. No data is
retained for all previously visited chunks.

Greedy faces merge only when material, direction, sunlight, and block-light level
all match. Light gradients split rectangles rather than bleeding across them.
Existing conforming edges, normals, and repeating UVs remain unchanged. Vertex
stride is now 44 bytes, so the two fixed GPU vertex buffers are **44 MiB each**.
The scene vertex cap and 512 KiB upload budget remain unchanged.

## Controls and demo

- **1** selects grass; **2** selects the lamp.
- **E** places the selected block against the aimed surface; **Q** removes it.
- **V** cycles materials, UVs, normals, raw sunlight, and raw block light. Both
  raw light views disable fog and display level 0 as black through level 15 as white.
- `voxel_game --block-light-demo` opens an isolated test world under `build/`
  with a lamp beneath a roof spanning a chunk boundary. Normal saved worlds are
  untouched. `--sunlight-demo` still provides the roof-and-skylight-hole fixture.

## Verification

`block_lighting` checks Manhattan attenuation in open air, all-axis chunk seams,
negative coordinates, corner paths, sealed walls, multiple emitters, source
removal, packed field queries, coordinate limits, invalid input, cancellation,
and concurrent builds. A separate per-source shortest-path oracle verifies a
randomly obstructed room. Mesh tests check that light levels are constant over
each merged rectangle.

`chunk_streaming` verifies a lamp in a non-drawn halo chunk, persistent illumination
after eviction/restart, sunlight independence, and removal without stale light.
The Vulkan smoke test places/removes lamps and exercises all five views. Windows
and Linux CI include these tests; sanitizer CI covers the standalone core.
