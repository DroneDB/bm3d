# Copyright (C) 2026 Digipa
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The tests, one program per label:
#   unit         the pieces that need no model: argument checks, the JSON checker, versions, the format
#                library (records, writer, reader, hash, metadata), the corto compressor and the
#                orders the builder gives equal keys
#   integration  models built through the library and read back from the file
#   cli          the program run as a process: arguments, exit codes, output, paths
#   determinism  the same input built again, and with another thread count, gives the same file
#   robustness   inputs that are not what the loaders expect, one at a time and damaged at random
#                (BM3D_MUTATION_RUNS sets the number of runs of the random ones, 500 by default)
#   golden       small models built by the program whose files are the fixtures of the other readers
#                of the format: their content hashes are compared with the ones in golden/, and the
#                files are written to BM3D_GOLDEN_DIR (by default <build folder>/golden)
# ctest -L unit (or integration, or cli, or determinism, or robustness, or golden) runs one label. A test that takes more than ten
# seconds would carry the label slow as well; none does yet.

find_package(GTest CONFIG REQUIRED)
find_package(Threads REQUIRED)
find_package(JPEG REQUIRED)
find_package(PNG REQUIRED)
if(BM3D_WITH_TIFF)
    find_package(TIFF REQUIRED)
endif()
find_package(WebP CONFIG REQUIRED)
find_package(Stb REQUIRED)
include(GoogleTest)

# What the tests share: temporary folders, a process runner, the writers of pictures and models,
# and the reader of the file the program writes (the one of src/format).
add_library(bm3d_test_support STATIC
    support/env_var_guard.cpp
    support/model_files.cpp
    support/bm3d_file.cpp
    support/pictures.cpp
    support/process_runner.cpp
    support/temp_dir.cpp
    support/version_file.cpp)
# the headers of src/format are included as format/<name>.h
target_include_directories(bm3d_test_support PUBLIC support ${PROJECT_SOURCE_DIR}/src PRIVATE ${Stb_INCLUDE_DIR})
target_link_libraries(bm3d_test_support
    PUBLIC GTest::gtest bm3d::bm3d
    PRIVATE bm3d_corto JPEG::JPEG PNG::PNG WebP::webp Threads::Threads)
if(BM3D_WITH_TIFF)
    # The tests write their TIFF textures with libtiff; without it they check the refusal
    target_link_libraries(bm3d_test_support PRIVATE TIFF::TIFF)
    target_compile_definitions(bm3d_test_support PUBLIC BM3D_WITH_TIFF)
endif()
target_compile_definitions(bm3d_test_support PUBLIC
    "BM3D_TEST_FIXTURES_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/fixtures\""
    "BM3D_TEST_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
if(MSVC)
    # stb_image_write opens files with fopen
    target_compile_definitions(bm3d_test_support PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(bm3d_test_support PRIVATE /W3)
    # The test sources hold UTF-8 text (the paths of other alphabets): read as UTF-8, not in the
    # code page of the machine. Public, so every test program reads its sources the same way.
    target_compile_options(bm3d_test_support PUBLIC /utf-8)
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
    unit/compressor_test.cpp
    unit/equal_keys_test.cpp
    unit/format_records_test.cpp
    unit/format_writer_reader_test.cpp
    unit/json_object_test.cpp
    unit/mesh_loader_registry_test.cpp
    unit/metadata_test.cpp
    unit/scratch_errors_test.cpp
    unit/sha256_test.cpp
    unit/simplification_test.cpp
    unit/version_test.cpp)
# The JSON checker is internal to the program: its header is not part of the library's interface.
# The headers of the builder include each other by their bare names.
target_include_directories(bm3d_unit_tests PRIVATE ${PROJECT_SOURCE_DIR}/src/format ${PROJECT_SOURCE_DIR}/src/build)
target_link_libraries(bm3d_unit_tests PRIVATE bm3d_corto bm3d_vcg)

bm3d_add_tests(bm3d_integration_tests integration
    integration/build_failure_test.cpp
    integration/format_output_test.cpp
    integration/leaf_precision_test.cpp
    integration/logger_test.cpp
    integration/mesh_formats_test.cpp
    integration/simplified_levels_test.cpp
    integration/texture_formats_test.cpp
    integration/texture_resolution_test.cpp)
# The decoders are internal to the program: the texture tests read pictures with them directly
target_include_directories(bm3d_integration_tests PRIVATE ${PROJECT_SOURCE_DIR}/src/build)

bm3d_add_tests(bm3d_cli_tests cli
    cli/cli_test.cpp
    cli/cli_paths_test.cpp)
# The program under test is built first and found by the path CMake gives it
add_dependencies(bm3d_cli_tests bm3d)
target_compile_definitions(bm3d_cli_tests PRIVATE "BM3D_TEST_EXE=\"$<TARGET_FILE:bm3d>\"")

bm3d_add_tests(bm3d_determinism_tests determinism
    determinism/determinism_test.cpp)
# Each build is a run of the program, as in the cli tests, so that no state is shared between them
add_dependencies(bm3d_determinism_tests bm3d)
target_compile_definitions(bm3d_determinism_tests PRIVATE "BM3D_TEST_EXE=\"$<TARGET_FILE:bm3d>\"")

# The inputs are run through the program, as in the cli tests. The mutation loop is a test for each seed
# so that ctest -j can run them together; with the sanitizers a run takes a good part of a second.
bm3d_add_tests(bm3d_robustness_tests robustness
    robustness/mutation_test.cpp
    robustness/outcome.cpp
    robustness/robustness_test.cpp)
target_link_libraries(bm3d_robustness_tests PRIVATE bm3d_corto)
target_include_directories(bm3d_robustness_tests PRIVATE robustness)
add_dependencies(bm3d_robustness_tests bm3d)
target_compile_definitions(bm3d_robustness_tests PRIVATE "BM3D_TEST_EXE=\"$<TARGET_FILE:bm3d>\"")

# The golden files: models built by the program, the fixtures of the other readers of the format.
# The target writes them (to BM3D_GOLDEN_DIR, by default <build folder>/golden) without any of the
# other tests.
set(BM3D_GOLDEN_DEFAULT_DIR "${CMAKE_BINARY_DIR}/golden")
bm3d_add_tests(bm3d_golden_tests golden
    golden/golden_test.cpp)
add_dependencies(bm3d_golden_tests bm3d)
target_compile_definitions(bm3d_golden_tests PRIVATE
    "BM3D_TEST_EXE=\"$<TARGET_FILE:bm3d>\""
    "BM3D_GOLDEN_DEFAULT_DIR=\"${BM3D_GOLDEN_DEFAULT_DIR}\""
    "BM3D_GOLDEN_HASHES=\"${CMAKE_CURRENT_SOURCE_DIR}/golden/hashes.txt\"")
add_custom_target(bm3d_golden_fixtures
    COMMAND $<TARGET_FILE:bm3d_golden_tests>
    DEPENDS bm3d_golden_tests
    COMMENT "Writing the golden files to BM3D_GOLDEN_DIR or ${BM3D_GOLDEN_DEFAULT_DIR}"
    VERBATIM)
