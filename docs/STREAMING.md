# Chunk streaming

The application now runs this pipeline:

```text
Integer player chunk + local position
    -> latest requested neighborhood
    -> generation / disk worker
    -> immutable snapshot batch + neighbor halo
    -> meshing worker
    -> bounded main-thread upload
    -> atomic scene publication
```

`ChunkStreamer` owns its World, generation thread, meshing thread, and save directory. Public World access is const. Player edits, regeneration, export, and import are bounded commands applied by the generation worker. GLFW and Vulkan stay on the main thread. Workers never call graphics APIs.

## Residency and memory bounds

The default draw neighborhood is 5 x 5 x 5 chunks. A one-chunk halo makes generation residency at most 7 x 7 x 7 = **343 chunks**, clipped at signed coordinate limits. Eviction precedes allocation; distant teleports cannot accumulate intermediate regions. Chunks unchanged since loading are regenerated or reloaded on return.

There is one latest position request, one pending snapshot batch, one pending completed mesh, one active job per worker, and at most 32 pending edit/file commands (plus the current worker's at-most-32-command batch). New requests replace older requests. Workers check revision/cancellation between chunks; stale meshes cannot reach the renderer. Movement does not append an unbounded queue.

CPU mesh output is capped at **1,048,576 vertices per scene**. Vulkan allocates two fixed **44 MiB** vertex buffers at startup (including both light attributes). One displays the complete previous scene while the other receives at most **512 KiB per render-loop pass**. No chunk-transition GPU allocation occurs. Temporary CPU chunks, snapshots, and mesh batches have bounded counts. Standard allocator/driver high-water memory may remain reserved; process working set is not expected to return to its startup value.

The vertex cap is explicit: pathological edited geometry exceeding it reports an error rather than allocating indefinitely or silently dropping faces. This is a small initial view distance with fixed capacity, not an unrestricted render-distance setting. Disk usage grows with edited chunks; travel through pristine terrain does not create chunk files.

## Seams and publication

Each mesh batch uses one complete immutable snapshot set, including its neighbor halo. Greedy faces retain Phase 3's matching unit edge segments, winding, normals, and repeating UVs. The mesh worker builds all visible chunks relative to the batch's integer origin. GPU publication swaps the whole batch only after the upload is complete and the previous frame fence is signaled. Neighbor edits never publish one side of a seam alone.

The generation worker supplies matching immutable [sunlight summaries](SUNLIGHT.md), including saved roofs above the loaded halo. Unchanged columns reuse cached heights; opacity edits invalidate their columns. Lighting and geometry are published together.

The meshing worker incrementally updates [block light](BLOCK_LIGHTING.md) from the
same snapshots after an initial full build. See [light updates](LIGHT_UPDATES.md)
for cancellation and lifetime rules. The existing halo contains the full level-15 light range, including
sources outside the drawn neighborhood. Packed light storage is 2 KiB per chunk.

Distance fog fades the finite draw frontier into the clear color. It adds no lighting. The previous complete scene remains visible while a replacement is prepared. A teleport or movement faster than generation throughput can outrun residency; the game remains responsive, but newly requested terrain takes time to appear. No finite worker system can promise immediately available terrain after arbitrary teleports.

## Player precision

The quaternion camera stores a signed integer chunk origin and a local float position in [0,16). Movement rebases at chunk crossings, including negative coordinates. Camera/mesh chunk origins are subtracted as integers before conversion to float. Meshing, camera transforms, and block targeting therefore preserve local precision beyond the exact integer range of doubles. The coordinate domain remains signed 64-bit block space, not mathematical infinity.

## Edits and files

Edits persist under `saves/streamed/v1-seed-<seed>/`, separated by seed and generator version. Edited chunks are written on eviction and orderly shutdown, then released from memory. Empty edited chunks are saved too. F5 exports the aimed chunk to that world's `export.vxc`; F9 reloads it if its coordinate is currently resident. Exports replace that one export slot. Generation, encoding, reading, and writing all happen on the generation worker.

Writes finish a temporary file before rotating the previous file to `.bak` and renaming the temporary file into place. Loading recovers `.bak` if the primary is missing. Files are size-bounded and CRC validated. Errors propagate to the main thread; failed eviction writes do not discard the live chunk. This is not an fsync/power-loss durability guarantee: edits not yet evicted/flushed can be lost in a process crash. One streamer/application may own a save directory at a time; concurrent processes sharing it are unsupported.

Accepted commands are queued, not immediate completion promises. Stale compare-and-exchange edits and commands for evicted chunks do not resurrect old residency. The UI reports a full command queue. Graceful `close()` stops/cancels jobs, drains accepted commands against resident data, flushes dirty chunks, joins both workers, and reports errors. Shutdown can wait for disk I/O; ordinary frames do not.

## Responsiveness and verification

Main-thread chunk work consists of posting the latest position, cheap bounded command submissions, consuming a finished mesh, and a fixed-size copy. It never joins or waits for worker jobs. Frame fences and swapchain acquisition are polled. Presentation still uses vsync; startup, window resizing/swapchain recreation, shutdown, OS scheduling, driver behavior, and disk failures are outside a hard real-time guarantee.

`chunk_streaming` verifies halo-derived meshes, residency/queue bounds during travel, cancellation under 2,000 position changes, deterministic regeneration, edit eviction/reload/restart, asynchronous export/import, extreme coordinates, full-radius geometry, immediate shutdown, and worker error propagation. Camera tests cover rebasing and relative transforms beyond double precision. The Vulkan smoke test uses the normal radius and traverses edits, neighboring and distant chunks, resize events, and all debug views, waiting for each requested revision to be published without blocking the render loop.

Run `ctest --test-dir build/windows -C Release --output-on-failure` on Windows, or `ctest --preset linux` after the Linux build. Linux sanitizer CI includes the standalone streaming tests with AddressSanitizer/UBSan and ThreadSanitizer.
