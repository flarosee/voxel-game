# Transparent blocks

Persisted IDs 7, 8, and 9 are water, glass, and leaves. Existing IDs remain stable;
unknown IDs retain opaque behavior. Select them with keys 3, 4, and 5, then place
with E or remove with Q. `--transparency-demo` creates an isolated world under
`build/` with three overlapping rows crossing the x=16 chunk seam.

## Materials and face rules

Water and glass use straight alpha blending (0.45 and 0.22). Leaves use a repeating
procedural cutout mask: holes discard fragments, solid portions write depth.
These are static full voxel blocks, without fluid flow, refraction, texture assets,
or ambient occlusion.

| Neighbor pair | Shared surface |
| --- | --- |
| Opaque / opaque | Culled |
| Matching water, glass, or leaves | Culled |
| Opaque / transparent | Keep the opaque face; cull the transparent face against it |
| Water / glass | One double-sided glass interface, chosen by stable material priority |
| Leaves / blended material | Keep the cutout and blended surfaces; depth rejects blend behind solid leaf pixels, holes reveal it |
| Any block / air or missing chunk | Keep the block face |

The same rules apply inside chunks and across the captured neighbor halo. Edits,
load/unload, and material replacement rebuild the relevant scene. Leaves and
blended faces are double-sided, so their interiors remain visible. Leaf/leaf
internal faces are intentionally culled to avoid dense internal cutout layers.

## Ordering and greedy compatibility

The renderer draws opaque geometry first, then cutouts with depth writes, then
blended surfaces with depth testing and no depth writes. It uses ordinary source
alpha / one-minus-source-alpha blending.

The meshing worker builds an axis-aligned binary space partition (BSP) of blended
rectangles for the whole visible scene. Rectangles crossing a partition plane are
split at integer coordinates. Each frame traverses the far child, coplanar faces,
then the near child according to the camera position relative to that plane.
This produces back-to-front ordering across chunks, including when the camera
is inside a transparent volume. Camera movement needs no re-upload or worker job.
Unlike face-center distance sorting, partitioning handles overlapping projections
of long greedy faces and intersecting face planes.

Greedy merging still requires equal material, direction, and both light levels.
Split fragments retain outward normals, light values, repeating UV phase, and
unit boundary segments. Mesh positions and partition planes use the same local
scene origin, preserving precision at distant world coordinates.

Workers create private geometry and ordering metadata, check cancellation, and
publish them together only after the complete budgeted upload. The vertex cap
also applies after splitting. Fragment counts and partition depth are bounded;
exceeding capacity reports the existing scene-budget error rather than publishing
a partial scene. The renderer retains the active immutable scene for its ordering
metadata, including its bounded CPU vertex copy. This correctness-first approach
can create more geometry and draw calls than approximate sorting.

## Lighting

All three materials transmit the existing direct vertical skylight. Block light
passes through them with the same one-level attenuation as air. Leaves use this
simple voxel-level lighting rule regardless of individual mask holes. Replacing
stone with glass invalidates sunlight and block-light opacity; replacing water
with glass does not change transmission. Transparent faces can sample their own
cell's light, including interior-facing surfaces. Lamps remain opaque emitters.

## Verification

`transparent_blocks` covers pairwise interfaces on all axes at positive and negative
chunk seams, neighbor removal, greedy slabs, UV preservation, split area, cancellation,
capacity limits, and light transmission updates. An independent ray/triangle oracle
checks the draw sequence along 1,500 rays through intersecting rectangles.
Streaming tests verify render-pass changes after edits, eviction, restart, and
removal. The Vulkan smoke fixture includes glass, water, and leaves with camera
movement and swapchain recreation. CI includes Windows, Linux, and core sanitizers.
