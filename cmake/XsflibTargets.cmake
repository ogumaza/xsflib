# SPDX-License-Identifier: MIT

# How each of xsflib's libraries and tests is built.

# Sets `variable` to the options that make every build compute the same floats. Fused multiply-add contraction is off.
# On CPUs with FMA, such as Apple Silicon, Clang contracts by default. MSVC's /fp:precise doesn't. On 32-bit x86,
# floating point uses SSE2, not the x87 unit and its extra precision. clang-cl takes MSVC's options but is Clang
# underneath. It takes Clang's settings through /clang:. MSVC's cl.exe does neither by default and gets no options.
# xsflib_set_flags and both modes of xsflib_project use them.
function(xsflib_float_options variable)
    set(options "")
    if (MSVC AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        list(APPEND options /clang:-ffp-contract=off)
        if (CMAKE_SIZEOF_VOID_P EQUAL 4)
            list(APPEND options /arch:SSE2)
        endif()
    elseif (NOT MSVC)
        list(APPEND options -ffp-contract=off)
        if (CMAKE_SIZEOF_VOID_P EQUAL 4 AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(i.86|x86|x86_64|AMD64)$")
            list(APPEND options -msse2 -mfpmath=sse)
        endif()
    endif()

    set(${variable} ${options} PARENT_SCOPE)
endfunction()

# Sets `variable` to the options that make __FILE__ give paths relative to `source` and `binary`, a build's source and
# build folders. Debug information keeps absolute paths so that debuggers find the sources. MSVC's cl.exe isn't
# supported and gets no options. xsflib_set_flags and xsflib_project's CHECK_COMPILERS mode use them.
function(xsflib_prefix_map_options variable source binary)
    set(options "")
    if (MSVC AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        list(APPEND options "/clang:-fmacro-prefix-map=${source}/=" "/clang:-fmacro-prefix-map=${binary}/=")
    elseif (NOT MSVC)
        list(APPEND options "-fmacro-prefix-map=${source}/=" "-fmacro-prefix-map=${binary}/=")
    endif()

    set(${variable} ${options} PARENT_SCOPE)
endfunction()

# Output must be the same on every platform. These flags go on xsflib's sources: the warnings and the float options
# (xsflib_float_options). They then hold however the library is built: as part of another project, or alone for
# find_package.
function(xsflib_set_flags target)
    if (MSVC)
        target_compile_options(${target} PRIVATE /W4 /utf-8)
        # fopen and freopen are standard C++. The _s versions that MSVC suggests instead aren't portable.
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra)
    endif()

    xsflib_float_options(float_options)
    target_compile_options(${target} PRIVATE ${float_options})

    # Windows: bzip2's header includes windows.h. Unless NOMINMAX is defined, windows.h defines min and max as macros.
    # Those macros break std::min, std::max and std::numeric_limits::max.
    if (WIN32)
        target_compile_definitions(${target} PRIVATE NOMINMAX)
    endif()

    # __FILE__ gives paths relative to the project's folders when the project (xsflib or a library that builds on it)
    # is built alone. A project that includes it sets the mapping for its build.
    if (CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
        xsflib_prefix_map_options(prefix_map_options ${PROJECT_SOURCE_DIR} ${PROJECT_BINARY_DIR})
        target_compile_options(${target} PRIVATE ${prefix_map_options})
    endif()
endfunction()

# Adds the static library <project>_<name> with the alias <project>::<name>, built from the given sources, for xsflib
# or a library that builds on it. The library's public headers are under the project's include folder.
function(xsflib_add_project_library project name)
    string(TOUPPER ${project} upper)
    add_library(${project}_${name} STATIC ${ARGN})
    add_library(${project}::${name} ALIAS ${project}_${name})
    target_include_directories(${project}_${name} PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
                                                         $<INSTALL_INTERFACE:include>
                                                  PRIVATE ${PROJECT_SOURCE_DIR}/src)
    target_compile_features(${project}_${name} PUBLIC cxx_std_20)
    set_target_properties(${project}_${name} PROPERTIES CXX_EXTENSIONS OFF EXPORT_NAME ${name})
    xsflib_set_flags(${project}_${name})
    set_property(GLOBAL APPEND PROPERTY ${upper}_INSTALLED_TARGETS ${project}_${name})
endfunction()

# Adds the test <project>_<name>_test from tests/<name>_test.cpp, linked with <project>::testing and the given
# libraries, if <PROJECT>_TESTS is on. A project without a <PROJECT>_TESTS option always gets the test. A project
# without <project>::testing links xsflib::testing.
function(xsflib_add_project_test project name)
    string(TOUPPER ${project} upper)
    if (DEFINED ${upper}_TESTS AND NOT ${upper}_TESTS)
        return()
    endif()

    if (TARGET ${project}::testing)
        set(testing ${project}::testing)
    else()
        set(testing xsflib::testing)
    endif()
    add_executable(${project}_${name}_test ${PROJECT_SOURCE_DIR}/tests/${name}_test.cpp)
    target_link_libraries(${project}_${name}_test PRIVATE ${testing} ${ARGN})
    set_target_properties(${project}_${name}_test PROPERTIES CXX_EXTENSIONS OFF)
    xsflib_set_flags(${project}_${name}_test)
    add_test(NAME ${project}_${name} COMMAND ${project}_${name}_test)
endfunction()

# Adds the static library xsflib_<name> with the alias xsflib::<name> (xsflib_add_project_library).
function(xsflib_add_library name)
    xsflib_add_project_library(xsflib ${name} ${ARGN})
endfunction()

# Adds the test xsflib_<name>_test from tests/<name>_test.cpp (xsflib_add_project_test).
function(xsflib_add_test name)
    xsflib_add_project_test(xsflib ${name} ${ARGN})
endfunction()

# Installs the project's headers, its libraries and the CMake package that find_package(<project>) reads, for xsflib
# or a library that builds on it. cmake/<project>Config.cmake.in is the package's configuration.
function(xsflib_install_package project)
    string(TOUPPER ${project} upper)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    get_property(targets GLOBAL PROPERTY ${upper}_INSTALLED_TARGETS)
    install(TARGETS ${targets} EXPORT ${project}Targets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/${project} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(EXPORT ${project}Targets NAMESPACE ${project}:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${project})

    configure_package_config_file(${PROJECT_SOURCE_DIR}/cmake/${project}Config.cmake.in
        ${PROJECT_BINARY_DIR}/${project}Config.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${project})
    write_basic_package_version_file(${PROJECT_BINARY_DIR}/${project}ConfigVersion.cmake
                                     COMPATIBILITY SameMajorVersion)
    install(FILES ${PROJECT_BINARY_DIR}/${project}Config.cmake ${PROJECT_BINARY_DIR}/${project}ConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${project})
endfunction()
