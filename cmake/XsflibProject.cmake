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

# Both modes use xsflib_float_options. CHECK_COMPILERS also uses xsflib_prefix_map_options.
include(${CMAKE_CURRENT_LIST_DIR}/XsflibTargets.cmake)

# Sets the C++ standard, a release build by default, the static C runtime and the compiler options. CHECK_COMPILERS
# rejects the unsupported compilers. On Windows, that's every compiler but clang-cl. Elsewhere, it's GCC and Clang for
# 32-bit x86. Their output would differ from other builds. The error names the project NAME. CHECK_COMPILERS also sets
# <PREFIX>_CLANG_CL for clang-cl. Unless NO_WARNINGS is given, it sets <PREFIX>_WARNINGS
# (xsflib_project_warnings). NOMINMAX defines NOMINMAX on Windows. WARNINGS_ON_EVERY_TARGET adds the warnings to every
# target instead.
macro(xsflib_project)
    cmake_parse_arguments(_xsflib_project "CHECK_COMPILERS;WARNINGS_ON_EVERY_TARGET;NOMINMAX;NO_WARNINGS" "NAME;PREFIX"
                          "" ${ARGN})

    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)

    if (_xsflib_project_CHECK_COMPILERS)
        # Reject unsupported compilers. Windows builds require clang-cl. The other compilers that take MSVC's options,
        # such as MSVC's cl.exe, are unsupported. On 32-bit x86, GCC and Clang use x87 floating point with extra
        # precision. Their output would then differ from other builds. clang-cl gets SSE2 there below.
        if (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
            set(_xsflib_project_compiler "MSVC's cl.exe")
        elseif (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            set(_xsflib_project_compiler GCC)
        else()
            set(_xsflib_project_compiler ${CMAKE_CXX_COMPILER_ID})
        endif()
        if (WIN32 AND NOT MSVC)
            message(FATAL_ERROR "${_xsflib_project_NAME} doesn't support ${_xsflib_project_compiler} on Windows, such "
                                "as MinGW's GCC or the GNU-style clang++. Build with Visual Studio's clang-cl (cmake -T "
                                "ClangCL). README.md has the steps.")
        endif()
        if (MSVC AND NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
            message(FATAL_ERROR "${_xsflib_project_NAME} doesn't support ${_xsflib_project_compiler}. Build with Visual "
                                "Studio's clang-cl (cmake -T ClangCL). README.md has the steps.")
        endif()
        if (NOT MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 4 AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(i.86|x86|x86_64|AMD64)$")
            message(FATAL_ERROR "${_xsflib_project_NAME} doesn't support 32-bit x86 builds with "
                                "${_xsflib_project_compiler}: their output would differ from other builds. Build for "
                                "64-bit instead, or on Windows use clang-cl. clang-cl builds 32-bit x86 with SSE2.")
        endif()
        if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
            set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
        endif()
        # MSVC: the static C runtime. A plugin then requires no runtime DLLs. The policy applies it in subprojects that
        # declare an older minimum CMake version too.
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        set(CMAKE_POLICY_DEFAULT_CMP0091 NEW)
        # clang-cl takes MSVC's options but is Clang underneath. It gets Clang's settings below through /clang:.
        if (MSVC AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
            set(${_xsflib_project_PREFIX}_CLANG_CL ON)
        endif()
        # Output must not depend on the platform: every build computes the same floats.
        xsflib_float_options(_xsflib_project_float_options)
        add_compile_options(${_xsflib_project_float_options})
        # __FILE__ gives paths relative to the source or build directory.
        xsflib_prefix_map_options(_xsflib_project_prefix_map_options ${CMAKE_SOURCE_DIR} ${CMAKE_BINARY_DIR})
        add_compile_options(${_xsflib_project_prefix_map_options})
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
            # Link the runtime statically so that a drag-and-drop executable runs without runtime DLLs.
            set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        else()
            add_compile_options(-Wall -Wextra)
        endif()
        # Every build computes the same floats.
        xsflib_float_options(_xsflib_project_float_options)
        add_compile_options(${_xsflib_project_float_options})
    else()
        message(FATAL_ERROR "xsflib_project: give CHECK_COMPILERS or WARNINGS_ON_EVERY_TARGET")
    endif()

    foreach (_xsflib_project_variable IN ITEMS CHECK_COMPILERS WARNINGS_ON_EVERY_TARGET NOMINMAX NO_WARNINGS NAME
                                               PREFIX UNPARSED_ARGUMENTS KEYWORDS_MISSING_VALUES compiler float_options
                                               prefix_map_options)
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
# submodule is missing. The command in the message also fetches the submodules that a library holds, such as xsflib's
# copy of seqlib. The caller sets the libraries' options before the call.
macro(xsflib_add_libraries)
    foreach (_xsflib_library IN ITEMS ${ARGN})
        if (NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/externals/${_xsflib_library}/CMakeLists.txt")
            message(FATAL_ERROR "externals/${_xsflib_library} is missing. Run: git submodule update --init --recursive")
        endif()
        add_subdirectory(externals/${_xsflib_library} EXCLUDE_FROM_ALL)
    endforeach()
    unset(_xsflib_library)
endmacro()
