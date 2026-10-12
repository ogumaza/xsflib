# SPDX-License-Identifier: MIT

# The settings that a project built on xsflib starts with, and the libraries it adds. A project includes this file from
# its submodule after project() and before it adds the libraries:
#
#     include(externals/xsflib/cmake/XsflibProject.cmake)
#     xsflib_project(CHECK_COMPILERS NAME xyz PREFIX XYZ NOMINMAX)
#     xsflib_add_libraries(xsflib)
#
# The settings keep the output the same on every platform. A project gets its warnings in one of two ways: in a
# variable that its targets take, or on every target.

# Sets the C++ standard, a release build by default, the static C runtime and the compiler options. CHECK_COMPILERS
# rejects the compilers whose output would differ from other builds. The error names the project NAME. CHECK_COMPILERS
# also sets <PREFIX>_CLANG_CL for clang-cl. Unless NO_WARNINGS is given, it sets <PREFIX>_WARNINGS
# (xsflib_project_warnings). NOMINMAX defines NOMINMAX on Windows. WARNINGS_ON_EVERY_TARGET adds the warnings to every
# target instead.
macro(xsflib_project)
    cmake_parse_arguments(_xsflib_project "CHECK_COMPILERS;WARNINGS_ON_EVERY_TARGET;NOMINMAX;NO_WARNINGS" "NAME;PREFIX"
                          "" ${ARGN})

    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)

    if (_xsflib_project_CHECK_COMPILERS)
        # Reject unsupported compilers. Windows builds require clang-cl or MSVC. On 32-bit x86, GCC and Clang use x87
        # floating point with extra precision. Their output would then differ from other builds. clang-cl and MSVC use
        # SSE2 there.
        if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            set(_xsflib_project_compiler GCC)
        else()
            set(_xsflib_project_compiler ${CMAKE_CXX_COMPILER_ID})
        endif()
        if (WIN32 AND NOT MSVC)
            message(FATAL_ERROR "${_xsflib_project_NAME} doesn't support ${_xsflib_project_compiler} on Windows, such "
                                "as MinGW's GCC or the GNU-style clang++. Build with Visual Studio's clang-cl (cmake -T "
                                "ClangCL) or with MSVC; README.md has the steps.")
        endif()
        if (NOT MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 4 AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(i.86|x86|x86_64|AMD64)$")
            message(FATAL_ERROR "${_xsflib_project_NAME} doesn't support 32-bit x86 builds with "
                                "${_xsflib_project_compiler}: their output would differ from other builds. Build for "
                                "64-bit instead, or on Windows use clang-cl or MSVC. They build 32-bit x86 with SSE2.")
        endif()
        if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
            set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
        endif()
        # MSVC: the static C runtime. A plugin then requires no runtime DLLs. The policy applies it in subprojects that
        # declare an older minimum CMake version too.
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        set(CMAKE_POLICY_DEFAULT_CMP0091 NEW)
        # clang-cl takes MSVC's options but is Clang underneath. It gets Clang's settings below through /clang: and
        # ignores MSVC's /d1trimfile.
        if (MSVC AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
            set(${_xsflib_project_PREFIX}_CLANG_CL ON)
        endif()
        # Output must not depend on the platform. Turn off fused multiply-add contraction so that every build computes
        # the same floats. On CPUs with FMA, such as Apple Silicon, Clang contracts by default. MSVC's /fp:precise
        # doesn't contract.
        if (${_xsflib_project_PREFIX}_CLANG_CL)
            add_compile_options(/clang:-ffp-contract=off)
            # clang-cl does 32-bit x86 floating point on the x87 unit unless it's given SSE2. MSVC uses SSE2 there by
            # default.
            if (CMAKE_SIZEOF_VOID_P EQUAL 4)
                add_compile_options(/arch:SSE2)
            endif()
        elseif (NOT MSVC)
            add_compile_options(-ffp-contract=off)
        endif()
        # __FILE__ gives paths relative to the source or build directory. Debug information keeps absolute paths so
        # that debuggers find the sources. MSVC's /d1trimfile isn't documented. Visual Studio 2019 16.4 and later
        # support it.
        if (${_xsflib_project_PREFIX}_CLANG_CL)
            add_compile_options("/clang:-fmacro-prefix-map=${CMAKE_SOURCE_DIR}/="
                                "/clang:-fmacro-prefix-map=${CMAKE_BINARY_DIR}/=")
        elseif (MSVC)
            cmake_path(NATIVE_PATH CMAKE_SOURCE_DIR _xsflib_project_source_dir)
            cmake_path(NATIVE_PATH CMAKE_BINARY_DIR _xsflib_project_binary_dir)
            add_compile_options(/d1trimfile:${_xsflib_project_source_dir} /d1trimfile:${_xsflib_project_binary_dir})
        else()
            add_compile_options(-fmacro-prefix-map=${CMAKE_SOURCE_DIR}/= -fmacro-prefix-map=${CMAKE_BINARY_DIR}/=)
        endif()
        if (NOT _xsflib_project_NO_WARNINGS)
            xsflib_project_warnings(${_xsflib_project_PREFIX})
        endif()
        # Windows: windows.h defines min and max as macros unless NOMINMAX is defined. Those macros break std::min,
        # std::max and std::numeric_limits::max.
        if (_xsflib_project_NOMINMAX AND WIN32)
            add_compile_definitions(NOMINMAX)
        endif()
    elseif (_xsflib_project_WARNINGS_ON_EVERY_TARGET)
        if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR AND NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
            set(CMAKE_BUILD_TYPE Release)
        endif()
        if (MSVC)
            add_compile_options(/W4 /utf-8)
            # clang-cl contracts floating-point operations into fused multiply-adds where the CPU has them. It also
            # does 32-bit x86 floating point on the x87 unit unless it's given SSE2. MSVC does neither.
            if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
                add_compile_options(/clang:-ffp-contract=off)
                if (CMAKE_SIZEOF_VOID_P EQUAL 4)
                    add_compile_options(/arch:SSE2)
                endif()
            endif()
            # Link the runtime statically so that a drag-and-drop executable runs without runtime DLLs.
            set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        else()
            # Disable FMA contraction. Clang enables it by default on CPUs such as Apple Silicon. Without it, results
            # are the same on every platform.
            add_compile_options(-Wall -Wextra -ffp-contract=off)
            # Use SSE2 on 32-bit x86 to avoid x87's extra precision. Results then match 64-bit x86.
            if (CMAKE_SIZEOF_VOID_P EQUAL 4 AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(i.86|x86|x86_64|AMD64)$")
                add_compile_options(-msse2 -mfpmath=sse)
            endif()
        endif()
    else()
        message(FATAL_ERROR "xsflib_project: give CHECK_COMPILERS or WARNINGS_ON_EVERY_TARGET")
    endif()

    foreach (_xsflib_project_variable IN ITEMS CHECK_COMPILERS WARNINGS_ON_EVERY_TARGET NOMINMAX NO_WARNINGS NAME
                                               PREFIX UNPARSED_ARGUMENTS KEYWORDS_MISSING_VALUES compiler source_dir
                                               binary_dir)
        unset(_xsflib_project_${_xsflib_project_variable})
    endforeach()
    unset(_xsflib_project_variable)
endmacro()

# Sets <prefix>_WARNINGS to the warnings that a PSF player's targets take. On MSVC, it also turns off the warnings that
# suggest the _s versions of standard functions such as fopen. The _s versions aren't portable.
macro(xsflib_project_warnings prefix)
    if (MSVC)
        set(${prefix}_WARNINGS /W4)
        add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
    else()
        set(${prefix}_WARNINGS -Wall -Wextra)
    endif()
endmacro()

# Adds each library in `externals`, in order, from the project's submodule of that name. Stops with a message if a
# submodule is missing. The caller sets the libraries' options before the call.
macro(xsflib_add_libraries)
    foreach (_xsflib_library IN ITEMS ${ARGN})
        if (NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/externals/${_xsflib_library}/CMakeLists.txt")
            message(FATAL_ERROR "externals/${_xsflib_library} is missing. Run: git submodule update --init")
        endif()
        add_subdirectory(externals/${_xsflib_library} EXCLUDE_FROM_ALL)
    endforeach()
    unset(_xsflib_library)
endmacro()
