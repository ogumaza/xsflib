# SPDX-License-Identifier: MIT

# The libraries that xsflib provides: with XSFLIB_PSF zlib (the PSF container), with XSFLIB_DISC_LIBRARIES zlib,
# Zstandard, liblzma and bzip2 (for a library that reads disc images), with XSFLIB_ZSTD Zstandard alone, and with
# XSFLIB_CLI libFLAC (FLAC output). Installed libraries are used where they exist. Otherwise each library is downloaded
# and built with xsflib. A universal macOS build (more than one architecture in CMAKE_OSX_ARCHITECTURES) builds
# Zstandard, liblzma and libFLAC from source. Installed copies, such as those from Homebrew, are built for the Mac's
# architecture only. zlib and bzip2 come with macOS for every architecture.
#
# Each library is also available as xsflib::zlib, xsflib::zstd, xsflib::lzma, xsflib::bzip2 and xsflib::flac. A
# project that uses xsflib links those rather than finding the libraries again.

include(FetchContent)
list(LENGTH CMAKE_OSX_ARCHITECTURES xsflib_osx_architectures)

# The libraries that had to be downloaded. An installed xsflib can't refer to them.
set(XSFLIB_FETCHED_DEPENDENCIES "")

# Adds the interface library xsflib_<name> with the alias xsflib::<name>, linked with the library that was found.
function(xsflib_add_dependency name library)
    add_library(xsflib_${name} INTERFACE)
    add_library(xsflib::${name} ALIAS xsflib_${name})
    target_link_libraries(xsflib_${name} INTERFACE ${library})
    set_target_properties(xsflib_${name} PROPERTIES EXPORT_NAME ${name})
    set_property(GLOBAL APPEND PROPERTY XSFLIB_INSTALLED_TARGETS xsflib_${name})
endfunction()

# A target that find_package creates is visible only in the folder that found it and below. A global target is visible
# to the projects that use xsflib too. A target that the parent project found is visible there already.
function(xsflib_make_global target)
    get_target_property(imported ${target} IMPORTED)
    get_target_property(global ${target} IMPORTED_GLOBAL)
    get_target_property(folder ${target} SOURCE_DIR)
    if (imported AND NOT global AND folder STREQUAL CMAKE_CURRENT_SOURCE_DIR)
        set_target_properties(${target} PROPERTIES IMPORTED_GLOBAL TRUE)
    endif()
endfunction()

# zlib.
if (XSFLIB_PSF OR XSFLIB_DISC_LIBRARIES)
    if (NOT TARGET ZLIB::ZLIB)
        find_package(ZLIB QUIET)
    endif()
    if (TARGET ZLIB::ZLIB)
        xsflib_make_global(ZLIB::ZLIB)
    else()
        FetchContent_Declare(zlib
            GIT_REPOSITORY https://github.com/madler/zlib.git
            GIT_TAG v1.3.1
            GIT_SHALLOW TRUE)
        set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(zlib)
        # Build only the static library. zlib's DLL would be built by default too. The DLL's resource script doesn't
        # compile with clang-cl.
        set_property(DIRECTORY ${zlib_SOURCE_DIR} PROPERTY EXCLUDE_FROM_ALL TRUE)
        target_include_directories(zlibstatic PUBLIC $<BUILD_INTERFACE:${zlib_SOURCE_DIR}>
                                                     $<BUILD_INTERFACE:${zlib_BINARY_DIR}>)
        add_library(ZLIB::ZLIB ALIAS zlibstatic)
        list(APPEND XSFLIB_FETCHED_DEPENDENCIES zlib)
    endif()
endif()

# Without either option, xsflib::zlib still wraps a zlib that the parent project found.
if (TARGET ZLIB::ZLIB)
    xsflib_add_dependency(zlib ZLIB::ZLIB)
endif()

if (XSFLIB_DISC_LIBRARIES OR XSFLIB_ZSTD)
    # Zstandard.
    if (NOT TARGET zstd::libzstd AND xsflib_osx_architectures LESS 2)
        find_package(zstd CONFIG QUIET)
    endif()
    if (TARGET zstd::libzstd)
        xsflib_make_global(zstd::libzstd)
        xsflib_add_dependency(zstd zstd::libzstd)
    else()
        FetchContent_Declare(zstd
            GIT_REPOSITORY https://github.com/facebook/zstd.git
            GIT_TAG v1.5.7
            GIT_SHALLOW TRUE
            SOURCE_SUBDIR build/cmake)
        foreach (option ZSTD_BUILD_PROGRAMS ZSTD_BUILD_TESTS ZSTD_BUILD_SHARED ZSTD_LEGACY_SUPPORT
                        ZSTD_MULTITHREAD_SUPPORT)
            set(${option} OFF CACHE BOOL "" FORCE)
        endforeach()
        set(ZSTD_BUILD_STATIC ON CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(zstd)
        set_property(DIRECTORY ${zstd_SOURCE_DIR}/build/cmake PROPERTY EXCLUDE_FROM_ALL TRUE)
        # Only in the build: zstd's project installs the target. An installed target can't name the build folder.
        target_include_directories(libzstd_static INTERFACE $<BUILD_INTERFACE:${zstd_SOURCE_DIR}/lib>)
        xsflib_add_dependency(zstd libzstd_static)
        list(APPEND XSFLIB_FETCHED_DEPENDENCIES zstd)
    endif()
endif()

if (XSFLIB_DISC_LIBRARIES)
    # liblzma.
    if (NOT TARGET LibLZMA::LibLZMA AND xsflib_osx_architectures LESS 2)
        find_package(LibLZMA QUIET)
    endif()
    if (TARGET LibLZMA::LibLZMA)
        xsflib_make_global(LibLZMA::LibLZMA)
        xsflib_add_dependency(lzma LibLZMA::LibLZMA)
    else()
        FetchContent_Declare(xz
            GIT_REPOSITORY https://github.com/tukaani-project/xz.git
            GIT_TAG v5.8.1
            GIT_SHALLOW TRUE)
        foreach (option XZ_TOOL_XZ XZ_TOOL_XZDEC XZ_TOOL_LZMADEC XZ_TOOL_LZMAINFO XZ_TOOL_SCRIPTS XZ_DOC XZ_NLS
                        BUILD_TESTING)
            set(${option} OFF CACHE BOOL "" FORCE)
        endforeach()
        FetchContent_MakeAvailable(xz)
        set_property(DIRECTORY ${xz_SOURCE_DIR} PROPERTY EXCLUDE_FROM_ALL TRUE)
        # xz turns on -Wall and many more warnings for Clang. clang-cl takes -Wall as -Weverything. liblzma is then
        # built without warnings, as libFLAC is.
        if (MSVC AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
            target_compile_options(liblzma PRIVATE /w)
        endif()
        # xz's project gives the header folder only to the library's sources. As for Zstandard, the library's users
        # get the folder here, in the build only.
        target_include_directories(liblzma INTERFACE $<BUILD_INTERFACE:${xz_SOURCE_DIR}/src/liblzma/api>)
        xsflib_add_dependency(lzma liblzma)
        list(APPEND XSFLIB_FETCHED_DEPENDENCIES xz)
    endif()

    # bzip2.
    if (NOT TARGET BZip2::BZip2)
        find_package(BZip2 QUIET)
    endif()
    if (TARGET BZip2::BZip2)
        xsflib_make_global(BZip2::BZip2)
        xsflib_add_dependency(bzip2 BZip2::BZip2)
    else()
        # bzip2 1.0.8 has no CMake build. The library is built from bzip2's sources here.
        FetchContent_Declare(bzip2
            URL https://sourceware.org/pub/bzip2/bzip2-1.0.8.tar.gz
            URL_HASH SHA256=ab5a03176ee106d3f0fa90e381da478ddae405918153cca248e682cd0c4a2269
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
        FetchContent_MakeAvailable(bzip2)
        add_library(xsflib_bzip2_library STATIC
            ${bzip2_SOURCE_DIR}/blocksort.c
            ${bzip2_SOURCE_DIR}/bzlib.c
            ${bzip2_SOURCE_DIR}/compress.c
            ${bzip2_SOURCE_DIR}/crctable.c
            ${bzip2_SOURCE_DIR}/decompress.c
            ${bzip2_SOURCE_DIR}/huffman.c
            ${bzip2_SOURCE_DIR}/randtable.c
        )
        target_include_directories(xsflib_bzip2_library PUBLIC $<BUILD_INTERFACE:${bzip2_SOURCE_DIR}>)
        # A project's warning options reach bzip2's sources too. WARNINGS_ON_EVERY_TARGET in xsflib_project adds /W4 to
        # every target, for example. bzip2 1.0.8 raises warnings under them: for unused parameters and the signs of
        # pointers, among others. xsflib can't fix them. Like liblzma and libFLAC, bzip2 is then built without warnings.
        if (MSVC AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
            target_compile_options(xsflib_bzip2_library PRIVATE /w)
        endif()
        xsflib_add_dependency(bzip2 xsflib_bzip2_library)
        list(APPEND XSFLIB_FETCHED_DEPENDENCIES bzip2)
    endif()
endif()

if (XSFLIB_CLI)
    # libFLAC comes from its CMake package (vcpkg and other CMake-built installs), then from pkg-config (Homebrew and
    # Linux distributions). If neither has it, it's fetched and built from source.
    set(xsflib_flac "")
    if (TARGET FLAC::FLAC)
        set(xsflib_flac FLAC::FLAC)
    elseif (xsflib_osx_architectures LESS 2)
        find_package(FLAC CONFIG QUIET)
        if (TARGET FLAC::FLAC)
            set(xsflib_flac FLAC::FLAC)
        else()
            find_package(PkgConfig QUIET)
            if (PKG_CONFIG_FOUND)
                pkg_check_modules(XSFLIB_FLAC QUIET IMPORTED_TARGET GLOBAL flac)
            endif()
            if (TARGET PkgConfig::XSFLIB_FLAC)
                set(xsflib_flac PkgConfig::XSFLIB_FLAC)
            endif()
        endif()
    endif()
    if (xsflib_flac)
        xsflib_make_global(${xsflib_flac})
    else()
        FetchContent_Declare(flac
            GIT_REPOSITORY https://github.com/xiph/flac.git
            GIT_TAG 1.5.0
            GIT_SHALLOW TRUE)
        foreach (option BUILD_CXXLIBS BUILD_PROGRAMS BUILD_EXAMPLES BUILD_TESTING BUILD_DOCS INSTALL_MANPAGES
                        INSTALL_PKGCONFIG_MODULES INSTALL_CMAKE_CONFIG_MODULE WITH_OGG)
            set(${option} OFF CACHE BOOL "" FORCE)
        endforeach()
        FetchContent_MakeAvailable(flac)
        # Build only libFLAC: the helper libraries of flac's programs would be built by default too.
        set_property(DIRECTORY ${flac_SOURCE_DIR} PROPERTY EXCLUDE_FROM_ALL TRUE)
        # flac turns on -Wall for Clang. clang-cl takes that as -Weverything. libFLAC is then built without warnings.
        if (MSVC AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
            target_compile_options(FLAC PRIVATE /w)
        endif()
        set(xsflib_flac FLAC::FLAC)
        list(APPEND XSFLIB_FETCHED_DEPENDENCIES flac)
    endif()
    xsflib_add_dependency(flac ${xsflib_flac})
endif()

# seqlib, for xsflib::testing's checks. A project that uses xsflib may add seqlib first. Otherwise xsflib adds the
# copy in externals/seqlib, or finds an installed seqlib where the copy is missing. The checks are headers alone. The
# copy's library is built only for a target that links it. An install requires installed copies of seqlib and the
# other libraries.
if (NOT TARGET seqlib::seqlib)
    if (XSFLIB_INSTALL)
        find_package(seqlib 1.0 REQUIRED)
    elseif (EXISTS ${PROJECT_SOURCE_DIR}/externals/seqlib/CMakeLists.txt)
        add_subdirectory(externals/seqlib EXCLUDE_FROM_ALL)
    else()
        # The copy is missing from a clone whose submodules weren't fetched recursively, such as a clone of a project
        # that holds xsflib as a submodule. The message gives the copy's folder relative to the top-level project.
        find_package(seqlib 1.0 QUIET)
        if (NOT seqlib_FOUND)
            file(RELATIVE_PATH xsflib_seqlib_folder ${CMAKE_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/externals/seqlib)
            message(FATAL_ERROR "${xsflib_seqlib_folder} is missing, and CMake found no installed seqlib. "
                                "Run: git submodule update --init --recursive")
        endif()
    endif()
endif()

if (NOT TARGET Threads::Threads)
    find_package(Threads REQUIRED)
endif()
xsflib_make_global(Threads::Threads)
