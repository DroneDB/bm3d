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

# The version of the file format the program writes (docs/FORMAT.md; 0 was the container of Nexus).
# src/format/bm3d_format.h holds the same number, and the library checks that the two agree.
set(BM3D_FORMAT_VERSION 1)

# PROJECT_IS_TOP_LEVEL does not exist before project(), which reads the toolchain file, so the
# same test is made by hand for the two things that must be known before it.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(_bm3d_top_level ON)
else()
    set(_bm3d_top_level OFF)
endif()

option(BM3D_BUILD_TESTS "Build the tests" ${_bm3d_top_level})
option(BM3D_INSTALL_DEVELOPMENT "Install the library, its header and the CMake package, not only the program" ${_bm3d_top_level})
# libtiff is the largest of the image libraries and the one few models need (photogrammetry
# textures are PNG or JPEG). Off, it is neither found nor linked, and a TIFF texture is refused
# with an error that says so.
option(BM3D_WITH_TIFF "Read TIFF textures (links libtiff)" ON)

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
    # libtiff and the test framework are features of the manifest: libtiff is a default one, so
    # that it is dropped by turning the defaults off (it is the only default feature), and the
    # test framework is installed only when the tests are built.
    if(BM3D_WITH_TIFF)
        list(APPEND VCPKG_MANIFEST_FEATURES tiff)
    else()
        set(VCPKG_MANIFEST_NO_DEFAULT_FEATURES ON)
    endif()
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
find_package(JPEG REQUIRED)
if(BM3D_WITH_TIFF)
    find_package(TIFF REQUIRED)
endif()
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
if(MSVC)
    target_compile_options(bm3d_vcg INTERFACE /bigobj)
endif()
if(WIN32)
    target_compile_definitions(bm3d_vcg INTERFACE NOMINMAX)
endif()

# --- The builder: the sources imported from Nexus, the VCGLib ply reader and the API.
add_library(bm3d_core STATIC
    third_party/vcg/wrap/ply/plylib.cpp
    src/api/bm3d.cpp
    src/api/error_mapping.cpp
    src/build/build_pipeline.cpp
    src/build/dag_ops.cpp
    src/build/fallback_policy.cpp
    src/build/hierarchy_builder.cpp
    src/build/input_reader.cpp
    src/build/kdtree.cpp
    src/build/level_builder.cpp
    src/build/mesh.cpp
    src/build/model_builder.cpp
    src/build/model_dag.cpp
    src/build/model_saver.cpp
    src/build/node_texture_baker.cpp
    src/build/node_texture_store.cpp
    src/build/texel_blanking.cpp
    src/build/texpyramid.cpp
    src/build/texture_encoder_pool.cpp
    src/build/tmesh.cpp
    src/common/cone.cpp
    src/common/mappedfile.cpp
    src/common/scratchfile.cpp
    src/common/textfile.cpp
    src/common/virtualarray.cpp
    src/compress/compressor.cpp
    src/format/bm3d_format.cpp
    src/format/bm3d_reader.cpp
    src/format/bm3d_writer.cpp
    src/format/json_object.cpp
    src/format/metadata.cpp
    src/format/sha256.cpp
    src/io/image/image.cpp
    src/io/image/image_decoder.cpp
    src/io/image/jpeg_codec.cpp
    src/io/image/stb_decoder.cpp
    src/io/image/tiff_decoder.cpp
    src/io/image/tone_map.cpp
    src/io/image/webp_codec.cpp
    src/io/mesh/mesh_loader_registry.cpp
    src/io/mesh/meshloader.cpp
    src/io/mesh/meshstream.cpp
    src/io/mesh/objloader.cpp
    src/io/mesh/plyloader.cpp
    src/io/mesh/stlloader.cpp)
add_library(bm3d::bm3d ALIAS bm3d_core)
set_target_properties(bm3d_core PROPERTIES
    EXPORT_NAME bm3d
    POSITION_INDEPENDENT_CODE ON)

# The public header is all a user of the library sees. The sources include each other by relative
# paths; src is on the path for the generated header and for the tests.
target_include_directories(bm3d_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}/generated"
        src)
# std::filesystem and the VCGLib headers need C++17
target_compile_features(bm3d_core PUBLIC cxx_std_17)
# The sources and the VCGLib headers hold UTF-8 text: MSVC reads them as UTF-8, not in the code
# page of the machine
target_compile_options(bm3d_core PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
# The image libraries stay private: a program that links bm3d_core does not need their headers.
# The stb headers are only included, so they are not part of the link (stb reads the PNG, BMP, GIF
# and TGA textures; libjpeg-turbo, libwebp and, optionally, libtiff read the others).
target_include_directories(bm3d_core PRIVATE ${Stb_INCLUDE_DIR})
target_link_libraries(bm3d_core
    PRIVATE
        bm3d_corto
        bm3d_vcg
        JPEG::JPEG
        WebP::webp
        Threads::Threads)
if(BM3D_WITH_TIFF)
    target_link_libraries(bm3d_core PRIVATE TIFF::TIFF)
    target_compile_definitions(bm3d_core PRIVATE BM3D_WITH_TIFF)
endif()
if(WIN32)
    target_link_libraries(bm3d_core PRIVATE netapi32 wsock32 ws2_32 userenv version winmm)
endif()

# --- The program
add_executable(bm3d cli/main.cpp)
target_include_directories(bm3d PRIVATE src/format)
target_link_libraries(bm3d PRIVATE bm3d_core Threads::Threads)
# UTF-8 sources, as for bm3d_core
target_compile_options(bm3d PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
if(MSVC)
    # The program reads one environment variable for its tests
    target_compile_definitions(bm3d PRIVATE _CRT_SECURE_NO_WARNINGS)
    # A .manifest among the sources is merged with the manifest the linker makes and embedded in
    # the program: it declares the program long path aware (the file says what that is for)
    target_sources(bm3d PRIVATE cli/bm3d.manifest)
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
