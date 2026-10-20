# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.20)
project(xsflib VERSION 1.4.0 LANGUAGES C CXX)

# CMake 3.20 has no PROJECT_IS_TOP_LEVEL. xsflib is the top-level project when the build starts in its folder.
if (CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(XSFLIB_TOP_LEVEL ON)
else()
    set(XSFLIB_TOP_LEVEL OFF)
endif()

option(XSFLIB_PSF "Build xsflib::psf and the player framework. They require zlib." ${XSFLIB_TOP_LEVEL})
option(XSFLIB_CLI "Build xsflib::cli. Its FLAC output requires libFLAC." ${XSFLIB_TOP_LEVEL})
option(XSFLIB_DISC_LIBRARIES "Find zlib, Zstandard, liblzma and bzip2 for a library that reads disc images." OFF)
option(XSFLIB_ZSTD "Provide Zstandard alone as xsflib::zstd." OFF)
option(XSFLIB_TESTS "Build xsflib's tests and register them with CTest." ${XSFLIB_TOP_LEVEL})
option(XSFLIB_INSTALL "Install xsflib's headers, libraries and CMake package. Requires installed dependencies." OFF)

if (XSFLIB_TOP_LEVEL AND NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)
include(XsflibDependencies)
include(XsflibTargets)
include(XsflibApps)

# The checks that the tests of xsflib and of the projects that use it share. They're seqlib::testing's checks under
# xsflib's names.
add_library(xsflib_testing INTERFACE)
add_library(xsflib::testing ALIAS xsflib_testing)
target_include_directories(xsflib_testing INTERFACE $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
                                                    $<INSTALL_INTERFACE:include>)
target_compile_features(xsflib_testing INTERFACE cxx_std_20)
target_link_libraries(xsflib_testing INTERFACE seqlib::testing)
set_target_properties(xsflib_testing PROPERTIES EXPORT_NAME testing)
set_property(GLOBAL APPEND PROPERTY XSFLIB_INSTALLED_TARGETS xsflib_testing)

if (XSFLIB_TESTS)
    enable_testing()
endif()

# The channel log and the WAV writer of play tests.
xsflib_add_test(channel_log)

# The common files: byte order, bounds-checked reads, ASCII, UTF-8 names, numbers as text, files and their UTF-8
# paths, WAV files, SHA-1, SHA-256 and memory images.
xsflib_add_library(common src/common/files.cpp src/common/sha1.cpp src/common/sha256.cpp src/common/text.cpp
                   src/common/utf8.cpp)
xsflib_add_test(binary xsflib::common)
xsflib_add_test(files xsflib::common)
xsflib_add_test(sha1 xsflib::common)
xsflib_add_test(sha256 xsflib::common)
xsflib_add_test(samples_sha256 xsflib::common)
xsflib_add_test(snapshot xsflib::common)
xsflib_add_test(wav xsflib::common seqlib::seqlib)

# The console of a command-line tool: the arguments in UTF-8, UTF-8 or line-buffered output, and a window that waits
# before it closes. It links nothing but Windows' shell32.
xsflib_add_library(console src/console/console.cpp)
if (WIN32)
    target_link_libraries(xsflib_console PRIVATE shell32) # CommandLineToArgvW
endif()
xsflib_add_test(console xsflib::console)

# The command-line tools: WAV and FLAC output, and the frames of a format's play tool and rip tool. The console comes
# from xsflib::console.
if (XSFLIB_CLI)
    xsflib_add_library(cli src/cli/audio_out.cpp)
    target_link_libraries(xsflib_cli PUBLIC xsflib::common xsflib::console PRIVATE xsflib::flac)
    xsflib_add_test(audio_out xsflib::cli xsflib::common xsflib::flac)
    if (XSFLIB_PSF)
        xsflib_add_test(play xsflib::cli xsflib::player)
        xsflib_add_test(rip_frame xsflib::cli xsflib::rip xsflib::psf)
    endif()
endif()

# The PSF container: the header, the compressed program, the tags and the variables tag. xsflib::psf_container requires
# no zlib. A reader with a decompressor of its own links only that. xsflib::psf adds zlib's decompressor and writer, and
# the programs of the formats.
xsflib_add_library(psf_container src/psf/container.cpp)
target_link_libraries(xsflib_psf_container PUBLIC xsflib::common)

if (XSFLIB_PSF)
    xsflib_add_library(psf src/psf/psf.cpp src/psf/program.cpp src/psf/streaming.cpp src/psf/descriptor.cpp)
    target_link_libraries(xsflib_psf PUBLIC xsflib::psf_container xsflib::common PRIVATE xsflib::zlib)
    xsflib_add_test(psf xsflib::psf xsflib::zlib)
    xsflib_add_test(psf_set xsflib::psf)
    xsflib_add_test(testing_sets xsflib::psf)
    xsflib_add_test(psf_descriptor xsflib::psf)
    xsflib_add_test(psf_streaming xsflib::psf xsflib::zlib)
endif()

# The player framework: playback with length, fade and seeking (a template on each format's player), and rendering
# ahead on a worker thread.
if (XSFLIB_PSF)
    xsflib_add_library(player src/player/render_ahead.cpp)
    target_link_libraries(xsflib_player PUBLIC xsflib::psf Threads::Threads)
    xsflib_add_test(render_ahead xsflib::player)
    xsflib_add_test(playback xsflib::player)
    xsflib_add_test(player_base xsflib::player)
    xsflib_add_test(archive_source xsflib::player)
    xsflib_add_test(player_modes xsflib::player)
endif()

# Code for rip tools: the names of a rip's files, the inputs' names and output folders, and the length analysis.
xsflib_add_library(rip src/rip/file_names.cpp src/rip/length.cpp src/rip/tool.cpp)
target_link_libraries(xsflib_rip PUBLIC xsflib::common)
xsflib_add_test(file_names xsflib::rip)
xsflib_add_test(rip_length xsflib::rip)

# Tests that use locally supplied files, from a local_tests.cmake in a folder of the source tree.
if (XSFLIB_TOP_LEVEL AND XSFLIB_TESTS)
    xsflib_include_local_tests()
endif()

if (XSFLIB_INSTALL)
    include(XsflibInstall)
endif()
