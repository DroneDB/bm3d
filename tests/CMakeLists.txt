# Copyright (C) 2026 Digipa
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The tests, one program per label:
#   unit         the pieces that need no model: argument checks, the JSON checker, versions
#   integration  models built through the library and read back from the file
#   cli          the program run as a process: arguments, exit codes, output, paths
# ctest -L unit (or integration, or cli) runs one label. A test that takes more than ten
# seconds would carry the label slow as well; none does yet.

find_package(GTest CONFIG REQUIRED)
find_package(Threads REQUIRED)
find_package(PNG REQUIRED)
find_package(TIFF REQUIRED)
find_package(WebP CONFIG REQUIRED)
find_package(Stb REQUIRED)
include(GoogleTest)

# What the tests share: temporary folders, a process runner, the writers of pictures and models,
# and the reader of the file the program writes.
add_library(bm3d_test_support STATIC
    support/env_var_guard.cpp
    support/model_files.cpp
    support/nexus_file.cpp
    support/pictures.cpp
    support/process_runner.cpp
    support/temp_dir.cpp
    support/version_file.cpp)
target_include_directories(bm3d_test_support PUBLIC support PRIVATE ${Stb_INCLUDE_DIR})
target_link_libraries(bm3d_test_support
    PUBLIC GTest::gtest bm3d::bm3d
    PRIVATE PNG::PNG TIFF::TIFF WebP::webp Threads::Threads)
target_compile_definitions(bm3d_test_support PUBLIC
    "BM3D_TEST_FIXTURES_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/fixtures\""
    "BM3D_TEST_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
if(MSVC)
    # stb_image_write opens files with fopen
    target_compile_definitions(bm3d_test_support PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(bm3d_test_support PRIVATE /W3)
else()
    target_compile_options(bm3d_test_support PRIVATE -Wall)
endif()

# bm3d_add_tests(<name> <label> <source>...): a test program of its own, whose tests carry the label
function(bm3d_add_tests name label)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE bm3d_test_support GTest::gtest_main)
    if(MSVC)
        target_compile_options(${name} PRIVATE /W3)
    else()
        target_compile_options(${name} PRIVATE -Wall)
    endif()
    gtest_discover_tests(${name}
        DISCOVERY_MODE PRE_TEST
        PROPERTIES LABELS ${label} TIMEOUT 300)
endfunction()

bm3d_add_tests(bm3d_unit_tests unit
    unit/build_options_test.cpp
    unit/json_object_test.cpp
    unit/version_test.cpp)
# The JSON checker is internal to the program: its header is not part of the library's interface
target_include_directories(bm3d_unit_tests PRIVATE ${PROJECT_SOURCE_DIR}/src/api)

bm3d_add_tests(bm3d_integration_tests integration
    integration/build_failure_test.cpp
    integration/mesh_formats_test.cpp
    integration/texture_formats_test.cpp
    integration/texture_resolution_test.cpp)

bm3d_add_tests(bm3d_cli_tests cli
    cli/cli_test.cpp
    cli/cli_paths_test.cpp)
# The program under test is built first and found by the path CMake gives it
add_dependencies(bm3d_cli_tests bm3d)
target_compile_definitions(bm3d_cli_tests PRIVATE "BM3D_TEST_EXE=\"$<TARGET_FILE:bm3d>\"")
