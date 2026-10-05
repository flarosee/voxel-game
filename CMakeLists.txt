cmake_minimum_required(VERSION 3.24)
project(VoxelGame VERSION 0.1.0 LANGUAGES C CXX)

include(CTest)
option(VOXEL_BUILD_APP "Build the Vulkan application (OFF builds only the standalone world core)" ON)
set(VOXEL_SANITIZER "none" CACHE STRING "Core checks: none, address, or thread (GCC/Clang)")
set_property(CACHE VOXEL_SANITIZER PROPERTY STRINGS none address thread)
find_package(Threads REQUIRED)
add_library(voxel_world
    src/world/BlockStorage.cpp src/world/Chunk.cpp src/world/World.cpp
    src/world/ChunkCodec.cpp src/world/Raycast.cpp
    src/terrain/Noise.cpp src/terrain/TerrainGenerator.cpp src/terrain/TerrainWindow.cpp
    src/mesh/GreedyMesher.cpp src/mesh/Transparency.cpp src/streaming/ChunkStreamer.cpp src/lighting/Sunlight.cpp src/lighting/BlockLight.cpp)
target_include_directories(voxel_world PUBLIC src)
target_compile_features(voxel_world PUBLIC cxx_std_20)
set_target_properties(voxel_world PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(voxel_world PUBLIC Threads::Threads)
if(MSVC)
    target_compile_options(voxel_world PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(voxel_world PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(NOT VOXEL_SANITIZER STREQUAL "none")
    if(MSVC OR NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(FATAL_ERROR "VOXEL_SANITIZER requires GCC or Clang")
    endif()
    if(VOXEL_SANITIZER STREQUAL "address")
        set(SANITIZER_FLAGS -fsanitize=address,undefined -fno-omit-frame-pointer)
    elseif(VOXEL_SANITIZER STREQUAL "thread")
        set(SANITIZER_FLAGS -fsanitize=thread -fno-omit-frame-pointer)
    else()
        message(FATAL_ERROR "Unknown VOXEL_SANITIZER")
    endif()
    target_compile_options(voxel_world PUBLIC ${SANITIZER_FLAGS})
    target_link_options(voxel_world PUBLIC ${SANITIZER_FLAGS})
endif()
if(BUILD_TESTING)
    add_executable(world_tests tests/WorldTests.cpp)
    target_link_libraries(world_tests PRIVATE voxel_world)
    add_test(NAME chunk_world COMMAND world_tests)
    set_tests_properties(chunk_world PROPERTIES TIMEOUT 120)
    add_executable(terrain_tests tests/TerrainTests.cpp)
    target_link_libraries(terrain_tests PRIVATE voxel_world)
    add_test(NAME terrain_generation COMMAND terrain_tests)
    set_tests_properties(terrain_generation PROPERTIES TIMEOUT 120)
    add_executable(mesh_tests tests/MeshTests.cpp)
    target_link_libraries(mesh_tests PRIVATE voxel_world)
    add_test(NAME greedy_meshing COMMAND mesh_tests)
    set_tests_properties(greedy_meshing PROPERTIES TIMEOUT 120)
    add_executable(streaming_tests tests/StreamingTests.cpp)
    target_link_libraries(streaming_tests PRIVATE voxel_world)
    add_test(NAME chunk_streaming COMMAND streaming_tests)
    set_tests_properties(chunk_streaming PROPERTIES TIMEOUT 120)
    add_executable(sunlight_tests tests/SunlightTests.cpp)
    target_link_libraries(sunlight_tests PRIVATE voxel_world)
    add_test(NAME sunlight COMMAND sunlight_tests)
    set_tests_properties(sunlight PROPERTIES TIMEOUT 120)
    add_executable(block_light_tests tests/BlockLightTests.cpp)
    target_link_libraries(block_light_tests PRIVATE voxel_world)
    add_test(NAME block_lighting COMMAND block_light_tests)
    set_tests_properties(block_lighting PROPERTIES TIMEOUT 120)
    add_executable(propagation_tests tests/PropagationTests.cpp)
    target_link_libraries(propagation_tests PRIVATE voxel_world)
    add_test(NAME light_updates COMMAND propagation_tests)
    set_tests_properties(light_updates PROPERTIES TIMEOUT 120)
    add_executable(transparency_tests tests/TransparencyTests.cpp)
    target_link_libraries(transparency_tests PRIVATE voxel_world)
    add_test(NAME transparent_blocks COMMAND transparency_tests)
    set_tests_properties(transparent_blocks PROPERTIES TIMEOUT 120)
endif()
if(NOT VOXEL_BUILD_APP)
    return()
endif()

include(FetchContent)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
set(VOLK_PULL_IN_VULKAN OFF CACHE BOOL "" FORCE)
set(VULKAN_HEADERS_ENABLE_MODULE OFF CACHE BOOL "" FORCE)

# Pin dependency revisions for reproducible builds. The first configure needs Git/network.
FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG a74efa0d5628b74adc0426af4c5710e287fa7c2c) # 3.4
FetchContent_Declare(vulkan_headers
    GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
    GIT_TAG b379292b2ab6df5771ba9870d53cf8b2c9295daf) # SDK 1.3.290.0
FetchContent_Declare(volk
    GIT_REPOSITORY https://github.com/zeux/volk.git
    GIT_TAG 466085407d5d2f50583fd663c1d65f93a7709d3e) # SDK 1.3.290.0
FetchContent_MakeAvailable(glfw vulkan_headers volk)
target_link_libraries(volk PUBLIC Vulkan::Headers)

set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLM_BUILD_LIBRARY OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 0af55ccecd98d4e5a8d1fad7de25ba429d60e863) # 1.0.1
# Build a shader compiler locally; no machine-wide Vulkan SDK is required.
set(ENABLE_OPT OFF CACHE BOOL "" FORCE)
set(ENABLE_HLSL OFF CACHE BOOL "" FORCE)
set(ENABLE_GLSLANG_BINARIES ON CACHE BOOL "" FORCE)
set(ENABLE_SPVREMAPPER OFF CACHE BOOL "" FORCE)
set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glslang
    GIT_REPOSITORY https://github.com/KhronosGroup/glslang.git
    GIT_TAG fa9c3deb49e035a8abcabe366f26aac010f6cbfb) # 14.3.0
FetchContent_MakeAvailable(glm glslang)

set(SHADER_HEADERS)
foreach(STAGE vert frag)
    set(HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated/cube_${STAGE}.hpp")
    add_custom_command(OUTPUT "${HEADER}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
        COMMAND $<TARGET_FILE:glslang-standalone> -V --target-env vulkan1.0
            --vn cube_${STAGE} -o "${HEADER}" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/cube.${STAGE}"
        DEPENDS glslang-standalone "${CMAKE_CURRENT_SOURCE_DIR}/shaders/cube.${STAGE}"
        VERBATIM)
    list(APPEND SHADER_HEADERS "${HEADER}")
endforeach()

add_executable(voxel_game src/main.cpp src/Application.cpp src/Application.hpp
    src/Renderer.cpp src/Renderer.hpp src/Camera.hpp ${SHADER_HEADERS})
target_include_directories(voxel_game PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_compile_features(voxel_game PRIVATE cxx_std_20)
set_target_properties(voxel_game PROPERTIES CXX_EXTENSIONS OFF)
target_compile_definitions(voxel_game PRIVATE GLFW_INCLUDE_NONE)
target_link_libraries(voxel_game PRIVATE glfw volk::volk glm::glm voxel_world)
if(MSVC)
    target_compile_options(voxel_game PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(voxel_game PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(camera_tests tests/CameraTests.cpp)
    target_include_directories(camera_tests PRIVATE src)
    target_compile_features(camera_tests PRIVATE cxx_std_20)
    target_link_libraries(camera_tests PRIVATE glm::glm)
    add_test(NAME quaternion_camera COMMAND camera_tests)
    add_test(NAME window_vulkan_smoke COMMAND voxel_game --smoke-test)
    set_tests_properties(window_vulkan_smoke PROPERTIES TIMEOUT 15)
endif()
