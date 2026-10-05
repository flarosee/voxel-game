# VoxelGame

A small C++20 / Vulkan starting point for Windows and Linux.

This milestone generates seeded terrain into sparse cubic chunks and builds sunlit
greedy meshes, with perspective, depth testing, and a
movable quaternion camera in a resizable 1280 x 720 window. The GPU appears in the
title and console. Rendering uses a FIFO (vsync) swapchain and recreates its color
and depth resources after resizing. Minimized windows pause rendering.

## Camera controls

| Control | Action |
| --- | --- |
| Hold right mouse button and move mouse | Look around; release to free the cursor |
| W / S | Move forward / backward along the view direction |
| A / D | Strafe left / right |
| Space / Left Ctrl | Move up / down along world Y |
| Left Shift | Move faster |
| R | Reset position and orientation above the terrain |
| E / Q | Place against / remove the block aimed at by the center of the window |
| 1 / 2 | Select grass / an emissive lamp for placement |
| 3 / 4 / 5 | Select water / glass / leaves |
| G | Regenerate the aimed chunk from the seed, resetting its edits |
| F5 | Export the aimed chunk to the current world's `export.vxc` slot |
| F9 | Reload that export if its chunk is resident |
| V | Cycle material, UV, normal, raw sunlight, and raw block-light views |
| Escape | Exit |

The camera stores a normalized `glm::quat`, not Euler angles. Incremental yaw
rotates about world Y; pitch rotates about camera-local X. The view matrix is the
inverse rotation followed by inverse translation. Pitch is limited to +/-89
degrees as a control choice to prevent flipping. There is no roll control.
Movement uses elapsed time and normalizes diagonal input. Blocks are axis-aligned.

## Chunk system

16 x 16 x 16 chunks use signed 64-bit block coordinates on all axes, with no fixed
height or depth. Uniform chunks need no block array; mixed chunks use packed
palette indices (516 bytes for two IDs), falling back to 8 KiB of direct IDs.
Thread-safe edits, immutable snapshots, and bounded, versioned chunk serialization
are implemented in a standalone library. Greedy meshing and direct voxel skylight are implemented.

See [the storage design, concurrency contract, file format, and API](docs/CHUNKS.md).

## Terrain

Stateless integer noise, grassland/desert biome implementations, and a deterministic
terrain generator produce identical chunks for the same seed and generator version.
Generation and meshing run on separate workers. The demo draws a 5 x 5 x 5
neighborhood with a generation halo, bounded queues, and budgeted main-thread uploads.
Edited chunks persist on disk instead of accumulating in RAM. An integer chunk
origin keeps camera and mesh positions precise during long-distance travel.
See [streaming architecture, limits, and verification](docs/STREAMING.md).

Choose a seed with `voxel_game --seed 12345` (default 12345). Terrain uses the
sunlit material colors and greedy chunk meshes. See [terrain design, seed compatibility,
streaming, and tests](docs/TERRAIN.md).

## Greedy meshing

Press **V** to cycle lit materials, a repeating UV checker, face normals, raw sunlight, and raw block light.
Edits and neighbor load/unload rebuild meshes automatically. See [meshing design
and correctness tests](docs/MESHING.md).

## Sunlight

Sunlight currently implements vertical sky occlusion only. Run
`voxel_game --sunlight-demo` for an isolated roof and skylight-hole test. See
[sunlight design, scope, and tests](docs/SUNLIGHT.md).

## Block lighting

Opaque lamps emit level 15, spreading through air with one level lost per step.
Press **2**, then **E** to place a lamp, or run `voxel_game --block-light-demo` for
an isolated lamp beneath a roof. See [block-light design and tests](docs/BLOCK_LIGHTING.md).
Lighting now propagates increases and decreases incrementally, with selective
skylight column updates. See [light updates](docs/LIGHT_UPDATES.md). Ambient
occlusion remains a later phase.

## Transparent blocks

Transparent blocks are available with **3 / 4 / 5**, then **E** to place. Run
`voxel_game --transparency-demo` for overlapping glass, water, and cutout leaves
across a chunk seam in an isolated test world. See [transparent rendering and
sorting](docs/TRANSPARENCY.md). Water is a static full block; fluid simulation,
refraction, and texture assets are outside this phase.

## Dependencies

- A C++20 compiler, CMake 3.24 or newer, Git, and Python 3 (for building glslang).
- A Vulkan-capable graphics driver/runtime.
- Internet access on the first CMake configure.

CMake downloads pinned revisions of GLFW 3.4, Vulkan-Headers, Volk, GLM 1.0.1, and
glslang 14.3.0 into the build directory. The first build includes the shader
compiler and takes longer; subsequent builds reuse it. GLSL shaders compile to
SPIR-V headers embedded in the executable, so no external shader files or special
working directory are required at runtime. Editing a shader rebuilds it automatically.
Volk loads Vulkan from the graphics driver at runtime, so a
separate Vulkan SDK is **not required** for this milestone. The application uses
Vulkan 1.0 features; the newer header version does not raise that runtime minimum.

## Windows

Install **Visual Studio 2022** with the **Desktop development with C++** workload,
including CMake tools and a Windows SDK. Install Git, Python 3, and an up-to-date GPU driver.
Use a **Developer PowerShell for VS 2022** terminal in this project folder:

```powershell
cmake --preset windows
cmake --build --preset windows --parallel
.\build\windows\Debug\voxel_game.exe
```

Alternatively, open this folder in Visual Studio and select the `windows` preset.
Keep the terminal console open
to see startup errors. A missing Vulkan runtime is usually resolved by installing
the graphics driver, rather than installing development headers.

## Linux

On Ubuntu 24.04 / Debian-based systems, install the build and window-system
dependencies:

```bash
sudo apt update
sudo apt install build-essential cmake ninja-build git pkg-config python3 \
    xorg-dev libwayland-dev libxkbcommon-dev libvulkan1 mesa-vulkan-drivers
```

Use the appropriate Vulkan driver for your GPU; NVIDIA systems may need their
distribution's proprietary NVIDIA driver instead of Mesa. Run from a graphical
desktop session:

```bash
cmake --preset linux
cmake --build --preset linux --parallel
./build/linux/voxel_game
```

GLFW builds both X11 and Wayland support by default. For an X11-only build:

```bash
cmake --preset linux -DGLFW_BUILD_WAYLAND=OFF
```

For Release builds, use `--config Release` with the Windows build command, or
configure Linux with `-DCMAKE_BUILD_TYPE=Release`.

## Verification

The terrain tests cover deterministic seeds, seam agreement, regeneration, and
streaming independently of rendering. The chunk tests cover coordinates, packed storage, serialization errors, and
concurrent edits/readers/unloads. The camera test checks initial framing, inverse view transforms, quaternion yaw,
pitch limits, normalization, and movement speed independent of frame rate.
Streaming tests cover bounded queues/residency, cancellation, disk-backed edits,
restart, and worker lifetime. The rendering smoke test presents at least 100
frames while exercising full-radius streaming, edits, export/import, distant
movement, diagnostic views, and resizing, then exits through normal cleanup. The rendering
test needs a working display and Vulkan driver:

```bash
ctest --preset windows  # Windows
ctest --preset linux    # Linux
```

For headless Linux testing, install `xvfb` and `xauth` in addition to the packages
above, then use Mesa's software Vulkan driver and a virtual X11 display:

```bash
xvfb-run -a ctest --preset linux
```

The GitHub Actions workflow builds Debug and Release on Windows and Linux, and
runs the Linux tests under Xvfb. Windows CI runs the camera, chunk, terrain, meshing, and streaming tests but skips the
rendering test because hosted runners are not guaranteed to have a usable Vulkan display. Interactive resizing,
Escape, and close-button behavior should also be checked on the target desktop.

## Project layout

- `CMakeLists.txt`: targets and pinned dependencies.
- `CMakePresets.json`: Windows and Linux build/test configurations.
- `src/main.cpp`: entry point and error reporting.
- `src/Application.*`: window, Vulkan setup, event loop, and resource cleanup.
- `src/Camera.hpp`: quaternion orientation, view transform, and movement.
- `src/Renderer.*`: mesh uploads, swapchain, depth buffers, pipeline, and frame synchronization.
- `src/mesh/`: snapshot-based greedy meshing, normals, and repeating UVs.
- `src/world/`: standalone chunk storage, concurrency, serialization, and integer-origin raycasts.
- `src/terrain/`: integer noise, biome interface, terrain generator, and streaming window.
- `src/streaming/`: bounded generation/meshing handoffs, residency, and edit persistence.
- `docs/STREAMING.md`: pipeline, memory budgets, publication, persistence, and limits.
- `docs/CHUNKS.md`: memory layout, thread-safety contract, file format, and limitations.
- `src/lighting/`: immutable skylight summaries and packed block-light fields.
- `shaders/cube.vert` / `cube.frag`: sunlight, materials, and diagnostic views.
- `tests/SunlightTests.cpp`: sky occlusion, merge boundaries, ray oracle, and concurrency.
- `tests/BlockLightTests.cpp`: attenuation, wall occlusion, graph oracle, and concurrency.
- `tests/MeshTests.cpp`: reference faces, geometry, seams, rebuild inputs, and concurrency.
- `tests/CameraTests.cpp`: camera math checks without a GPU or window.
- `tests/WorldTests.cpp`: chunk model, corruption, boundary, and concurrent-lifetime checks.
- `tests/TerrainTests.cpp`: seed fixtures, seams, regeneration, concurrency, and streaming checks.
- `tests/StreamingTests.cpp`: cancellation, memory bounds, edit persistence, and worker shutdown.
- `.github/workflows/build.yml`: Windows/Linux build checks.

This deliberately uses one frame in flight and a pair of fixed-capacity vertex
buffers. Workers rebuild complete scene batches;
the renderer publishes them after a bounded upload. Skylight and block light are implemented;
ambient occlusion, texture atlases, and collision are not. Distance
fog hides the finite draw frontier.

## References

- [GLFW Vulkan integration](https://www.glfw.org/docs/3.4/vulkan_guide.html)
- [GLFW build dependencies](https://www.glfw.org/docs/latest/compile.html)
- [Volk](https://github.com/zeux/volk)
- [GLM quaternion operations](https://glm.g-truc.net/0.9.9/api/a00663.html)
- [Vulkan presentation semaphore reuse](https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html)
