# Greedy meshing

`mesh::build` accepts immutable chunk snapshots and returns one chunk-local mesh per snapshot. It has no graphics dependencies or shared mutable state, so independent calls may run concurrently while the world is edited. The application now uses `mesh::buildOne` on a meshing worker with a complete neighbor halo; see [streaming](STREAMING.md).

Faces follow the [transparent material interface rules](TRANSPARENCY.md); missing neighbors count as air. Six directional slice masks are scanned width-first, then height, merging only equal material IDs, sunlight levels, and block-light levels. Rectangles never cross chunk boundaries. Neighbor lookup guards the signed coordinate limits instead of wrapping.

## Geometry and UV contract

Positions occupy [0,16] within a chunk. Normals are exact outward unit axis vectors, and triangles wind counterclockwise from outside. Vertices carry position, normal, two UV components, the original material ID, sunlight, and block light.

| Face | U | V |
| --- | --- | --- |
| +X / -X | +Y / -Y | Z |
| +Y / -Y | +Z / -Z | X |
| +Z / -Z | +X / -X | Y |

Coordinates in this table are chunk-local. One UV unit equals one block, even across merged rectangles. Negative UVs are intentional. Repeating textures have matching phase across chunk borders because origins are multiples of 16. No atlas is implemented. Press **V** to cycle lit materials, a repeating UV checker with colored U/V borders, normals mapped to RGB, raw sunlight, and raw block light.

Rectangle boundaries are subdivided at every voxel edge and triangulated as a center fan. This deliberately adds triangles to remove T-junctions between unequal greedy rectangles, perpendicular surfaces, and adjacent chunks. All local positions are exact integers or half-integers. Tests compare surface coverage with an independent face-by-face oracle and require paired triangle edges on closed manifold fixtures, including terrain and chunk seams.

## Rebuild and lifetime rules

The streaming controller assigns a revision to position changes and edit commands. Generation captures immutable snapshots, and meshing rebuilds a complete visible batch, including affected neighbors. Stale revisions are discarded. The standalone `sameSnapshots` helper remains available for identity-based comparisons; callers retain shared owners to prevent pointer reuse from hiding changes.

All faces in a rebuild read the same captured set, so both sides of a boundary agree. World snapshots are not a globally atomic multi-chunk transaction; edits arriving during capture become visible in subsequent frames. The captured set itself remains immutable and internally meshes consistently.

Vulkan polls the previous frame fence before writing an inactive preallocated buffer. Uploads are budgeted across frames, and complete batches swap together. Empty scenes issue no draw. All chunk vertices use the same world-to-clip transform, avoiding different per-chunk matrix rounding along shared borders.

Phase 4 adds bounded worker handoffs and a floating origin while retaining full scene rebuilds and the same correctness-first geometry. Sunlight now adds a light level to the merge key and vertex data; see [sunlight](SUNLIGHT.md). There is no indexed-buffer optimization. The camera uses an integer chunk origin plus a small local position, and mesh vertices remain relative to a nearby integer origin.

## Verification

`greedy_meshing` tests empty and solid chunks, cavities, material boundaries, negative coordinates, stepped chunk seams, seeded random fixtures, generated terrain, coordinate extremes, invalid input, snapshot changes, and concurrent meshing during live edits/unloads. It checks exact surface coverage, material IDs, winding, normals, UVs, area, and conforming edges.

The Vulkan smoke test exercises material/UV/normal shaders and asserts rebuilds after edits, replacement, and neighbor load/unload. Existing storage, serialization, terrain, and camera tests remain enabled. Windows and Linux CI include meshing; sanitizer CI uses the standalone core.
