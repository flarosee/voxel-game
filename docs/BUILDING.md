# Reproducible builds

The repository has one authoritative build description: CMake. IDE project files
must not be committed because they bypass target flags, generated shaders, pinned
dependencies, and tests. Visual Studio, CLion, VS Code CMake Tools, and other CMake
IDEs should open the repository root and consume `CMakePresets.json`.

## Supported build lanes

| Host | Preset | Compiler / purpose |
| --- | --- | --- |
| Windows x64 | `windows` | Visual Studio 2022 / MSVC, Debug by default |
| Windows x64 | `windows-release` | Same generated tree, Release configuration |
| Linux | `linux-gcc` | Explicit GCC Debug lane |
| Linux | `linux-clang` | Explicit Clang Debug lane |
| Linux | `linux-release` | Environment compiler, optimized build |
| Linux | `core` | World/tests only; no graphics dependencies or network fetch |

The existing `linux` and `windows` names remain stable for scripts and IDEs.

```bash
cmake --preset linux-gcc
cmake --build --preset linux-gcc --parallel
ctest --preset linux-gcc
```

On Windows, run the equivalent `windows` commands from a Developer PowerShell.
For a different IDE generator, open the root `CMakeLists.txt`; do not translate it
to a `.sln`, `.vcxproj`, Makefile, or IDE-owned build model.

## Compiler contract

Every project-owned target goes through `cmake/ProjectOptions.cmake`, including
tests. It requires C++20 without compiler extensions and applies the same warning
baseline. MSVC additionally gets conforming preprocessor/language modes, UTF-8,
and protection from Windows `min`/`max` macros. Set
`-DVOXEL_WARNINGS_AS_ERRORS=ON` for a locally strict build; it is opt-in so new
compiler warning releases do not make normal builds unusable.

A bare single-config configure defaults to Debug. In-source builds are rejected so
generated files cannot contaminate or shadow source files.

## Dependencies and offline builds

Application dependencies are pinned to immutable Git revisions with FetchContent.
The first application configure requires Git and network access. Later configures
can be forced offline with `-DFETCHCONTENT_FULLY_DISCONNECTED=ON` after the sources
have been populated. CMake's standard `FETCHCONTENT_SOURCE_DIR_<NAME>` cache values
can point at reviewed local mirrors for GLFW, Vulkan-Headers, Volk, GLM, and glslang.

The `core` preset avoids those downloads entirely and is the fastest way to validate
serialization, world logic, threading, meshing, lighting, and portability on a new
compiler or IDE.

Never edit files beneath `build/` or `cmake-build-*`; delete the affected build
directory or use `cmake --fresh --preset <name>` when changing compilers. A compiler
must never be switched inside an existing configured build tree.
