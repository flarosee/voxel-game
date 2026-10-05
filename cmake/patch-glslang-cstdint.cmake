# Backport glslang e40c14a3 for compatibility with GCC 15 and newer.
# FetchContent runs this script with the dependency source as its working directory.
set(spv_builder "SPIRV/SpvBuilder.h")
file(READ "${spv_builder}" contents)
if(NOT contents MATCHES "#include <cstdint>")
    string(FIND "${contents}" "#include <algorithm>" include_position)
    if(include_position EQUAL -1)
        message(FATAL_ERROR "Could not patch ${spv_builder}: include anchor not found")
    endif()
    string(REPLACE "#include <algorithm>" "#include <algorithm>\n#include <cstdint>"
        contents "${contents}")
    file(WRITE "${spv_builder}" "${contents}")
endif()
