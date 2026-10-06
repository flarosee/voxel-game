# Working in VoxelGame

## Where changes belong

| Area | Location | Responsibility |
| --- | --- | --- |
| Startup and game loop | `src/main.cpp`, `src/Application.*`, `ApplicationOptions.hpp` | Arguments, named options, lifetime order, frame coordination |
| Platform runtime | `src/platform/Runtime.*` | Window/device creation and exception-safe cleanup |
| Game session and controls | `src/game/GameSession.*`, `InputController.*` | Camera/world ownership, streaming coordination, input edges and interactions |
| Scripted smoke verification | `src/game/SmokeTest.*` | Test stages, completion criteria, timeouts |
| GPU rendering | `src/Renderer.*`, `shaders/` | World drawing, swapchain resources, UI render integration |
| Core logging | `src/Console.*` | Thread-safe bounded messages and terminal output; no ImGui dependency |
| Developer UI | `src/ui/` | Theme, panel layout, selection, input, and UI backend lifetime |
| World systems | `src/world/`, `terrain/`, `mesh/`, `lighting/`, `streaming/` | Blocks, generation, geometry, lighting, worker scheduling |
| Verification | `tests/` | Behavior checks and integration tests |
| Build definitions | `CMakeLists.txt`, `CMakePresets.json`, `cmake/` | Sources, dependencies, configurations, IDE presentation |

`voxel_game` links `voxel_world`, `voxel_core`, and `voxel_imgui`. The world
library stays independent of ImGui. Core logging owns history; the UI reads
new messages and presents them. Workers publish data or queue changes instead
of drawing widgets. UI methods run on the main thread.

## Building and checking

For Visual Studio, use `windows`; for CLion with the configured MSVC toolchain,
use `clion-windows`. See the README for toolchain setup.

```powershell
cmake --preset windows
cmake --build --preset windows --parallel
ctest --preset windows --output-on-failure
```

The graphical smoke test briefly launches the game and needs a working Vulkan
driver/display. For a console-only change, build and run the focused checks:

```powershell
cmake --build --preset windows --target voxel_game console_panel_tests --parallel
ctest --preset windows -R "console_panel|window_vulkan_smoke" --output-on-failure
```

Headers should list the public interface and required includes. Put storage,
synchronization, and substantial non-template implementation in `.cpp` files.
Split mixed-responsibility functions into named steps, keeping lifecycle and
thread ownership clear. Keep formatting changes separate from behavior changes
when practical. Add tests for observable behavior rather than private details.

Before committing, inspect `git status` and `git diff`, run relevant checks, and
use `git diff --check` to catch whitespace errors. Commit source, tests, build
definitions, and documentation together when they describe one change. Build
outputs and machine-specific IDE settings stay local. Do not edit generated
Visual Studio projects to change the build; update CMake instead.

## Cleanup audit and next passes

The first cleanup moves logging history out of the public header, centralizes
bounded-line insertion, separates console input drawing from window layout,
and gives the console test a timeout. Existing behavior remains covered by
the interaction and graphical smoke tests.

Further work should remain small and independently reviewable:

1. Console row rendering, mouse selection, selection scrolling, and clipboard
   actions now have named functions separate from `drawMessages()`. Further
   changes should preserve UTF-8 boundaries, clipping, and focus behavior.
2. Application input and block interactions now live in `InputController`;
   session state lives in `GameSession`, and the scripted stages live in
   `SmokeTest`. Application coordinates these concrete collaborators rather
   than inheriting a large hierarchy. Collaborators borrow explicit references;
   no interface is forced on a system with only one implementation.
3. Clarify renderer/UI resize and shutdown contracts. GPU resources must be
   released only after outstanding work is complete, before device destruction.
4. Expand dense world/streaming code where names and control flow obscure
   locking, cancellation, and ownership. Preserve those concurrency contracts.
5. Consolidate repeated CMake test setup and keep new source listings in the
   build definition rather than the IDE-layout helper.

Avoid broad renaming or moving every directory at once. Each pass should state
what became clearer and which behavior checks establish that it still works.
