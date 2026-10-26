# Copyright (C) 2026 Digipa
# SPDX-License-Identifier: GPL-3.0-or-later
#
# bm3d builds on its own and as a subproject of another CMake project (add_subdirectory). As a
# subproject it sets no toolchain, no global flag and no output directory: the program lands
# where the parent puts its programs.

cmake_minimum_required(VERSION 3.21)

# The version is the text of VERSION, which vcpkg.json repeats (keep the two in step: the release
# check compares them). project() takes the numeric part.
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/VERSION" BM3D_VERSION LIMIT_COUNT 1)
string(STRIP "${BM3D_VERSION}" BM3D_VERSION)
if(NOT BM3D_VERSION MATCHES "^([0-9]+\\.[0-9]+\\.[0-9]+)")
    message(FATAL_ERROR "VERSION must start with major.minor.patch, not '${BM3D_VERSION}'")
endif()
set(BM3D_VERSION_NUMERIC "${CMAKE_MATCH_1}")

# The version of the file format the program writes: 0 is the Nexus container.
set(BM3D_FORMAT_VERSION 0)

# PROJECT_IS_TOP_LEVEL does not exist before project(), which reads the toolchain file, so the
# same test is made by hand for the two things that must be known before it.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(_bm3d_top_level ON)
else()
    set(_bm3d_top_level OFF)
endif()

option(BM3D_BUILD_TESTS "Build the tests" ${_bm3d_top_level})
option(BM3D_INSTALL_DEVELOPMENT "Install the library, its header and the CMake package, not only the program" ${_bm3d_top_level})

if(_bm3d_top_level)
    # The dependencies come from vcpkg (manifest mode, vcpkg.json). A parent project that adds
    # this one has chosen its toolchain already, and so has a standalone build given
    # CMAKE_TOOLCHAIN_FILE; otherwise a standalone build takes vcpkg's from VCPKG_ROOT.
    if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
        if(DEFINED ENV{VCPKG_ROOT})
            set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
        else()
            message(FATAL_ERROR "Set CMAKE_TOOLCHAIN_FILE to vcpkg's scripts/buildsystems/vcpkg.cmake, or the VCPKG_ROOT environment variable")
        endif()
    endif()
    # The test framework is a feature of the manifest: it is installed only when the tests are built.
    if(BM3D_BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_LIST_DIR}/tests/CMakeLists.txt")
        list(APPEND VCPKG_MANIFEST_FEATURES tests)
    endif()
endif()

project(bm3d VERSION ${BM3D_VERSION_NUMERIC} LANGUAGES CXX)

if(PROJECT_IS_TOP_LEVEL AND NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

include(GNUInstallDirs)

find_package(Threads REQUIRED)
find_package(Eigen3 REQUIRED)
find_package(JPEG REQUIRED)
find_package(PNG REQUIRED)
find_package(TIFF REQUIRED)
find_package(WebP CONFIG REQUIRED)
find_package(Stb REQUIRED)  # vcpkg's stb: Stb_INCLUDE_DIR, headers only

configure_file(include/bm3d/version.h.in "${CMAKE_CURRENT_BINARY_DIR}/generated/bm3d/version.h" @ONLY)

# --- corto, the mesh codec. Built the way its own CMakeLists builds it: C++11 without extensions.
add_library(bm3d_corto STATIC
    third_party/corto/src/bitstream.cpp
    third_party/corto/src/color_attribute.cpp
    third_party/corto/src/corto_codec.cpp
    third_party/corto/src/cstream.cpp
    third_party/corto/src/decoder.cpp
    third_party/corto/src/encoder.cpp
    third_party/corto/src/normal_attribute.cpp
    third_party/corto/src/tunstall.cpp)
target_include_directories(bm3d_corto
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/third_party/corto/include>
    PRIVATE third_party/corto/include/corto)
set_target_properties(bm3d_corto PROPERTIES
    CXX_STANDARD 11
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_options(bm3d_corto PRIVATE /W3)
    target_compile_definitions(bm3d_corto PRIVATE _CRT_SECURE_NO_DEPRECATE)
else()
    target_compile_options(bm3d_corto PRIVATE -Wall -pedantic)
endif()

# --- VCGLib, header only but for wrap/ply/plylib.cpp, which bm3d_core compiles. No OpenMP: it is
# not looked for, so VCGLib hands nothing to a thread team; the builder runs its own threads.
add_library(bm3d_vcg INTERFACE)
target_include_directories(bm3d_vcg INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/third_party/vcg>)
target_link_libraries(bm3d_vcg INTERFACE Eigen3::Eigen)
if(MSVC)
    target_compile_options(bm3d_vcg INTERFACE /bigobj)
endif()
if(WIN32)
    target_compile_definitions(bm3d_vcg INTERFACE NOMINMAX)
endif()

# --- The builder: the imported Nexus sources, the VCGLib ply reader and the API.
add_library(bm3d_core STATIC
    third_party/vcg/wrap/ply/plylib.cpp
    src/api/bm3d.cpp
    src/api/json_object.cpp
    src/common/cone.cpp
    src/common/mappedfile.cpp
    src/common/nexusdata.cpp
    src/common/scratchfile.cpp
    src/common/stdnexusfile.cpp
    src/common/textfile.cpp
    src/common/traversal.cpp
    src/common/virtualarray.cpp
    src/nxsbuild/colormap.cpp
    src/nxsbuild/image.cpp
    src/nxsbuild/imageio.cpp
    src/nxsbuild/imagejpeg.cpp
    src/nxsbuild/kdtree.cpp
    src/nxsbuild/mesh.cpp
    src/nxsbuild/meshloader.cpp
    src/nxsbuild/meshstream.cpp
    src/nxsbuild/nexusbuilder.cpp
    src/nxsbuild/objloader.cpp
    src/nxsbuild/plyloader.cpp
    src/nxsbuild/stlloader.cpp
    src/nxsbuild/texpyramid.cpp
    src/nxsbuild/tmesh.cpp
    src/nxsbuild/tsloader.cpp
    src/nxsbuild/tsploader.cpp
    src/nxsedit/extractor.cpp
    src/nxszip/abitstream.cpp
    src/nxszip/atunstall.cpp
    src/nxszip/meshcoder.cpp
    src/nxszip/meshdecoder.cpp)
add_library(bm3d::bm3d ALIAS bm3d_core)
set_target_properties(bm3d_core PROPERTIES
    EXPORT_NAME bm3d
    POSITION_INDEPENDENT_CODE ON)

# The public header is all a user of the library sees. The Nexus sources include each other by
# relative paths, and src/api includes the ones of src/nxsbuild by their bare names.
target_include_directories(bm3d_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}/generated"
        src
        src/nxsbuild)
# std::filesystem and the VCGLib headers need C++17
target_compile_features(bm3d_core PUBLIC cxx_std_17)
# The sources and the VCGLib headers hold UTF-8 text: MSVC reads them as UTF-8, not in the code
# page of the machine
target_compile_options(bm3d_core PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
# The image libraries stay private: a program that links bm3d_core does not need their headers.
# The stb headers are only included, so they are not part of the link.
target_include_directories(bm3d_core PRIVATE ${Stb_INCLUDE_DIR})
target_link_libraries(bm3d_core
    PRIVATE
        bm3d_corto
        bm3d_vcg
        JPEG::JPEG
        PNG::PNG
        TIFF::TIFF
        WebP::webp
        Threads::Threads)
if(WIN32)
    target_link_libraries(bm3d_core PRIVATE netapi32 wsock32 ws2_32 userenv version winmm)
endif()

# --- The program
add_executable(bm3d cli/main.cpp)
target_include_directories(bm3d PRIVATE src/api)
target_link_libraries(bm3d PRIVATE bm3d_core Threads::Threads)
# UTF-8 sources, as for bm3d_core
target_compile_options(bm3d PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
if(MSVC)
    # The program reads one environment variable for its tests
    target_compile_definitions(bm3d PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# --- Installation: the program only, unless the development files are asked for
install(TARGETS bm3d RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

if(BM3D_INSTALL_DEVELOPMENT)
    include(CMakePackageConfigHelpers)
    install(TARGETS bm3d_core bm3d_corto bm3d_vcg
        EXPORT bm3dTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
    install(FILES
        include/bm3d/bm3d.h
        "${CMAKE_CURRENT_BINARY_DIR}/generated/bm3d/version.h"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/bm3d)
    install(EXPORT bm3dTargets
        NAMESPACE bm3d::
        FILE bm3dTargets.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/bm3d)
    configure_package_config_file(cmake/bm3d-config.cmake.in
        "${CMAKE_CURRENT_BINARY_DIR}/bm3d-config.cmake"
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/bm3d)
    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/bm3d-config-version.cmake"
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion)
    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/bm3d-config.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/bm3d-config-version.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/bm3d)
endif()

# --- Tests (the directory arrives with them)
if(BM3D_BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_LIST_DIR}/tests/CMakeLists.txt")
    enable_testing()
    add_subdirectory(tests)
endif()
