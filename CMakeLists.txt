cmake_minimum_required(VERSION 3.22)

project(BfrAss
    VERSION 1.0.0
    DESCRIPTION "NintendoWare BFRES (Wii U / Switch) importer for Assimp"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(BFRASS_ASSIMP_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../assimp"
    CACHE PATH "Assimp source tree that BfrAss is built against")
option(BFRASS_BUILD_CLI "Build the bfrass command line tool" ON)

if(NOT EXISTS "${BFRASS_ASSIMP_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "Assimp sources not found at ${BFRASS_ASSIMP_SOURCE_DIR}")
endif()

# Assimp is built statically with its bundled zlib so the resulting tool has no
# runtime dependency on system libraries beyond the C++ runtime.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ZLIB ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
set(ASSIMP_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
set(ASSIMP_INJECT_DEBUG_POSTFIX OFF CACHE BOOL "" FORCE)
set(ASSIMP_IGNORE_GIT_HASH ON CACHE BOOL "" FORCE)
set(ASSIMP_NO_EXPORT OFF CACHE BOOL "" FORCE)
add_subdirectory("${BFRASS_ASSIMP_SOURCE_DIR}" assimp EXCLUDE_FROM_ALL)

add_library(bfrass_core STATIC
    src/core/BinaryReader.cpp
    src/core/Log.cpp
    src/core/Math.cpp
    src/core/Yaz0.cpp
    src/bfres/ResFileLoader.cpp
    src/bfres/SwitchLoader.cpp
    src/bfres/WiiULoader.cpp
    src/bfres/VertexFormat.cpp
    src/bfres/Skeleton.cpp
    src/bfres/Dump.cpp
    src/texture/TextureFormat.cpp
    src/texture/TegraSwizzle.cpp
    src/texture/Gx2Surface.cpp
    src/texture/Bntx.cpp
    src/texture/Ftex.cpp
    src/texture/DecodeBc.cpp
    src/texture/DecodeBc6h.cpp
    src/texture/DecodeBc7.cpp
    src/texture/DecodeAstc.cpp
    src/texture/DecodeEtc.cpp
    src/texture/DecodeRaw.cpp
    src/texture/TextureDecoder.cpp
    src/texture/ImageWriter.cpp
    src/texture/TextureExporter.cpp
    src/importer/BfresImporter.cpp
    src/importer/SceneBuilder.cpp
    src/importer/MaterialBuilder.cpp
)

target_include_directories(bfrass_core
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    PRIVATE
        "${BFRASS_ASSIMP_SOURCE_DIR}/contrib/zlib"
        "${CMAKE_CURRENT_BINARY_DIR}/assimp/contrib/zlib"
)
target_link_libraries(bfrass_core PUBLIC assimp PRIVATE zlibstatic)

if(MSVC)
    target_compile_options(bfrass_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(bfrass_core PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
endif()

if(BFRASS_BUILD_CLI)
    add_executable(bfrass-cli src/cli/main.cpp)
    set_target_properties(bfrass-cli PROPERTIES OUTPUT_NAME bfrass)
    target_link_libraries(bfrass-cli PRIVATE bfrass_core)
    if(NOT MSVC)
        target_compile_options(bfrass-cli PRIVATE -Wall -Wextra)
    endif()
endif()
