# Cross-platform contract

Windows/MSVC and Linux/GCC are first-class targets. `CMakePresets.json` and CI build
both Debug and Release configurations. Core code is C++20 and does not depend on a
native object layout, native integer width, native byte order, or OS path separator.

## Stable data boundaries

- Persisted and renderer-facing integer fields use `std::uintN_t`/`std::intN_t`.
  `std::size_t` is only used for in-process container sizes and indices.
- VXC1 is encoded field-by-field in little-endian order. It never writes a C++
  object representation, enum, `bool`, padding, or a native pointer.
- Serialized enum types must declare an explicit fixed-width underlying type.
- Binary files are always opened with `std::ios::binary`.
- `Portability.hpp` rejects implementations without 8-bit bytes and the required
  exact-width integer types. `portability_contract` locks down core field widths
  and the on-disk signed/little-endian representation.

## Text and paths

- Internal text is UTF-8 in `std::string`/`std::string_view`. Native wide strings
  belong only in a future Windows platform adapter, never in core data structures.
- Paths are `std::filesystem::path` values assembled with `/` or `operator/`.
  Never build a path with a hard-coded `\\` separator.
- MSVC compiles source as UTF-8 (`/utf-8`). `.gitattributes` keeps source text
  normalized while declaring common asset and save formats as binary.

## Platform and Vulkan boundary

- There is currently no OS-specific code. If it becomes necessary, place it under
  `src/platform/`; `_WIN32` must not spread through world, renderer, or game logic.
- GLFW owns the window-system split. Volk loads the Vulkan loader and entry points
  dynamically; the game does not link directly to `vulkan-1` or `libvulkan`.
- GLFW and Vulkan stay on the main thread. Shutdown stops frame submission, closes
  and joins the streaming workers, then the renderer waits for the device and
  destroys GPU resources before `Application` destroys the device and window.

When a format or protocol is added, give it a version, byte order, explicit bounds,
and golden-byte tests before shipping it.
