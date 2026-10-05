# Sunlight: direct voxel skylight

This phase implements **vertical sky exposure**, using levels **0** (blocked) and
**15** (open sky). Water, glass, and leaves transmit skylight; other non-air blocks are opaque. Sunlight travels straight down
an unobstructed X/Z column without attenuation; the highest opaque block stops
it. Top faces sample the air above, side faces sample adjacent air, and opaque
undersides receive zero. This is voxel skylight, not an angled sun shadow map.

The sunlight channel has no lateral light spread,
ambient brightness floor, ambient occlusion, or day/night
cycle. Covered areas can therefore be completely black even beside an opening.
The existing distance fog remains a display effect and is disabled in the raw
sunlight diagnostic view. A separate [block-light channel](BLOCK_LIGHTING.md) now
provides illumination from lamps without changing these sunlight rules.

## Data and streaming

`lighting::Sunlight` keeps one optional highest-opaque-block height per world X/Z
column in the resident horizontal footprint. At the default radius this is at
most 49 tiles of 16 x 16 columns, independent of world height or travel distance.
No light array is added to each stored voxel. The height summary is exact for
direct vertical skylight; later propagated lighting can add separate storage.

The generation worker reuses unchanged column summaries and invalidates columns
whose opacity changes. New or invalidated columns are resolved from procedural
terrain, immutable resident snapshots, and saved overrides. Removed surface columns are resolved
downward through overrides until an opaque block or unmodified procedural layer
is found. Saved roofs above the loaded halo also contribute their opaque heights.
Resident data wins over its older saved version. Thus unloaded terrain is never
blindly treated as open sky, and removing a roof can restore sunlight.

Saved-file names are streamed from the world's directory; only files in the
current horizontal footprint are decoded. Disk reads use an eight-entry cache.
Memory remains bounded, but scanning directory metadata takes longer as the
number of saved chunks grows. A persistent spatial index is a future improvement;
all current I/O and computation stay off the render thread. Existing VXC1 files
and backup recovery work unchanged. Sunlight itself is derived, not serialized.

The complete sunlight summary travels with its matching immutable snapshot batch
to the meshing worker. Cancellation is checked during construction. Edits use the
existing full-scene geometry rebuild path with [incremental light updates](LIGHT_UPDATES.md).
Stale scene revisions remain rejected, and GPU
publication still swaps a complete geometry/light batch together.

## Meshing and rendering

Greedy merging now requires equal **material, face direction, and sunlight**.
This splits faces at shadow boundaries and preserves the existing unit-edge
triangulation, UVs, and normals. Each vertex carries a sunlight attribute, passed
flat to the fragment shader. Material color is multiplied by `sunlight / 15`.
UV and normal diagnostics bypass this multiplication.

Vertex stride is now 44 bytes including the separate block-light attribute. With
the existing 1,048,576-vertex scene cap, each GPU buffer is **44 MiB**. The 512 KiB upload budget is
unchanged. Coordinates remain integer-based until conversion to nearby mesh
positions; sunlight queries avoid overflow at signed coordinate limits.

## Try it

Run `voxel_game --sunlight-demo` for an isolated test world under `build/`. It
places a roof across the x=16 chunk boundary, with a one-block opening. The ground
beneath is dark, the opening admits a bright patch, and the roof's underside is
dark. E/Q can place/remove blocks and trigger a full rebuild. This demo does not
modify the normal `saves/streamed` world.

**V** cycles materials, UV checker, normals, raw sunlight, and raw block light.
In both raw views, white is level 15 and black is level 0; fog is disabled.

## Verification

`sunlight` compares greedy face illumination with independent vertical-ray tests
and covers open sky, roofs, undersides, skylight boundaries, negative/extreme
coordinates, procedural surface removal, cancellation, and concurrent immutable
reads. `chunk_streaming` checks a roof 128 blocks above terrain through eviction,
restart, `.bak` recovery, and removal. The Vulkan smoke test includes the roof
fixture and all four display modes. Existing storage, terrain, mesh, camera, and
streaming tests remain enabled. Linux CI includes sunlight and sanitizer tests.
