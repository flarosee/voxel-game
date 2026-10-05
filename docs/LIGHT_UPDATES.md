# Light propagation updates

The meshing worker retains its last completed block-light field and its immutable
input snapshots. The initial field uses `BlockLight::build`; later batches use
`BlockLight::updated`. Light remains derived data and is not serialized.

## Block light

Unchanged chunk storage skips voxel comparison. Changed chunks are compared for
opacity or emission changes; opaque material-only edits need no propagation.
New chunks start dark and enqueue their cells. Removed chunks enqueue the
surviving side of their boundaries to remove any lost light support.

A deduplicated queue reevaluates each affected cell: opaque cells have their
emission level (zero except lamps), and transmitting cells (air, water, glass, leaves) take the brightest neighboring level
minus one, clamped to zero. A changed value enqueues its neighbors. This handles
both increases and decreases, including overlapping lamps and newly opened or
sealed paths. Strict attenuation prevents cycles from retaining light after a
source disappears. Processing continues until the queue is empty.

Each chunk retains a packed 2 KiB light field. The temporary pending bitset uses
512 bytes per chunk and the queue holds at most one entry per resident voxel at
a time. Cells can be processed repeatedly while darkening converges. Old and new
fields coexist during an update, along with shared ownership of their input
storage; memory depends on bounded residency, not distance traveled.

## Sunlight

Direct vertical skylight retains its existing 0/15 rules. Opacity edits invalidate
their exact X/Z column; regeneration and import invalidate the chunk's 256 columns.
New footprint columns and invalid columns resolve their highest blocker again.
Unchanged columns reuse their heights. Resident and saved roofs are still folded
into the summary, including roofs above the loaded halo. Saved-file metadata
scanning and roof folding still occur; this phase does not add a disk spatial index
or lateral skylight spread.

Invalidation lives on the generation worker and survives cancelled jobs, including
edits to chunks evicted before the next completed scene. Standalone callers using
the optional sunlight cache must explicitly invalidate opacity changes and use
the same generator and override world.

## Publication and scope

Block-light updates construct a private replacement without mutating the previous
field. Cancellation discards partial work. Only a converged field replaces the
worker cache, and its retained snapshots allow direct comparison across skipped
revisions. Independent updates can read the same completed field concurrently.
The sunlight cache belongs only to the generation worker; published copies remain
immutable. No light queue runs on the render thread.

Geometry still rebuilds as a complete scene on the meshing worker. The main thread
uploads within its existing budget and swaps matching geometry and lighting only
when complete. This change does not introduce incremental mesh uploads, ambient
occlusion, or new lighting rules.

## Verification

`light_updates` compares every resident voxel with a fresh full-build reference
after source edits, overlapping lights, walls and openings, chunk unload/reload,
random edit batches, and distant residency changes. It also checks immutable old
fields, cancellation, concurrent updates, unchanged snapshots, material-only edits,
and selective sunlight roof addition/removal. Existing streaming persistence and
Vulkan smoke tests exercise the integrated path. Windows and Linux CI include the
new test, with standalone sanitizer coverage.
