include_guard(GLOBAL)

option(VOXEL_WARNINGS_AS_ERRORS "Treat warnings in VoxelGame-owned targets as errors" OFF)

function(voxel_validate_toolchain)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC" OR
       CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang|AppleClang)$")
        set(supported TRUE)
    else()
        set(supported FALSE)
    endif()
    if(NOT supported)
        message(WARNING
            "Compiler '${CMAKE_CXX_COMPILER_ID}' is not exercised by VoxelGame CI. "
            "CMake will still use its C++20 feature detection.")
    endif()
    message(STATUS
        "VoxelGame toolchain: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}; "
        "generator: ${CMAKE_GENERATOR}; system: ${CMAKE_SYSTEM_NAME}/${CMAKE_SYSTEM_PROCESSOR}")
endfunction()

function(voxel_configure_target target)
    target_compile_features(${target} PRIVATE cxx_std_20)
    set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF CXX_STANDARD_REQUIRED YES)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4 /permissive- /EHsc /utf-8 /Zc:__cplusplus /Zc:preprocessor)
        target_compile_definitions(${target} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
        if(VOXEL_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
        if(VOXEL_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()
