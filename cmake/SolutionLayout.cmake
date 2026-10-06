include_guard(GLOBAL)

# IDE organization only; files and build dependencies stay in place.
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set_property(GLOBAL PROPERTY PREDEFINED_TARGETS_FOLDER "Build Support/CMake")
set(CTEST_TARGETS_FOLDER "Build Support/CTest")
set_property(DIRECTORY "${PROJECT_SOURCE_DIR}" PROPERTY VS_STARTUP_PROJECT voxel_game)

function(voxel_group_directory directory)
    get_property(targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(target IN LISTS targets)
        if(target STREQUAL "voxel_game" OR target STREQUAL "voxel_world" OR target STREQUAL "voxel_core")
            set(folder "Game")
        elseif(target MATCHES "_tests$")
            set(folder "Tests")
        else()
            set(folder "Dependencies")
        endif()
        set_target_properties(${target} PROPERTIES FOLDER "${folder}")
    endforeach()
    get_property(children DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
    foreach(child IN LISTS children)
        voxel_group_directory("${child}")
    endforeach()
endfunction()

function(voxel_solution_layout)
    voxel_group_directory("${PROJECT_SOURCE_DIR}")
    source_group("Logging" FILES src/Console.cpp src/Console.hpp)
    target_sources(voxel_world PRIVATE
        src/Portability.hpp src/Types.hpp
        src/world/BlockStorage.hpp src/world/BlockTypes.hpp src/world/Chunk.hpp
        src/world/ChunkCodec.hpp src/world/Coordinates.hpp src/world/Raycast.hpp src/world/World.hpp
        src/terrain/Biome.hpp src/terrain/Noise.hpp src/terrain/TerrainGenerator.hpp src/terrain/TerrainWindow.hpp
        src/mesh/GreedyMesher.hpp src/mesh/Transparency.hpp
        src/streaming/ChunkStreamer.hpp src/lighting/Sunlight.hpp src/lighting/BlockLight.hpp)
    get_target_property(world_sources voxel_world SOURCES)
    list(REMOVE_ITEM world_sources src/Types.hpp src/Portability.hpp)
    source_group(TREE "${PROJECT_SOURCE_DIR}/src" PREFIX "Source" FILES ${world_sources})
    source_group("Shared" FILES src/Types.hpp src/Portability.hpp)
    if(TARGET voxel_game)
        source_group("Application" FILES src/main.cpp src/Application.cpp src/Application.hpp)
        source_group("Application" FILES src/ApplicationOptions.hpp)
        source_group("Platform" FILES src/platform/Runtime.cpp src/platform/Runtime.hpp)
        source_group("Game" FILES src/game/InputController.cpp src/game/InputController.hpp
            src/game/GameSession.cpp src/game/GameSession.hpp)
        source_group("Diagnostics" FILES src/game/SmokeTest.cpp src/game/SmokeTest.hpp)
        source_group("Rendering" FILES src/Renderer.cpp src/Renderer.hpp)
        source_group("Camera" FILES src/Camera.hpp)
        source_group("UI" FILES src/ui/Theme.cpp src/ui/Theme.hpp src/ui/Overlay.cpp src/ui/Overlay.hpp
            src/ui/ConsolePanel.cpp src/ui/ConsolePanel.hpp)
        source_group("Shared" FILES src/Console.hpp src/Types.hpp)
        target_sources(voxel_game PRIVATE shaders/cube.vert shaders/cube.frag)
        set_source_files_properties(shaders/cube.vert shaders/cube.frag PROPERTIES HEADER_FILE_ONLY TRUE)
        source_group("Shaders" FILES shaders/cube.vert shaders/cube.frag)
        source_group("Shaders/Generated" FILES ${SHADER_HEADERS})
    endif()
endfunction()

# Apply after all game, test, and dependency targets have been declared.
cmake_language(DEFER DIRECTORY "${PROJECT_SOURCE_DIR}" CALL voxel_solution_layout)
