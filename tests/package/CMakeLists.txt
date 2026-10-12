# SPDX-License-Identifier: MIT

# A project that uses xsflib, to check the CMake package. XSFLIB_ROUTE selects how the project gets xsflib:
# "subdirectory" adds the source folder, "fetchcontent" declares it with FetchContent, and "find_package" finds an
# installed copy. To check a route, configure this folder with -DXSFLIB_ROUTE=<route>, build it and run ctest.
cmake_minimum_required(VERSION 3.20)
project(xsflib_package_test LANGUAGES C CXX)

set(XSFLIB_ROUTE "find_package" CACHE STRING "How to get xsflib: subdirectory, fetchcontent or find_package")
set(XSFLIB_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." CACHE PATH "xsflib's source folder")

if (XSFLIB_ROUTE STREQUAL "subdirectory")
    set(XSFLIB_PSF ON)
    add_subdirectory(${XSFLIB_SOURCE_DIR} xsflib EXCLUDE_FROM_ALL)
elseif (XSFLIB_ROUTE STREQUAL "fetchcontent")
    set(XSFLIB_PSF ON)
    include(FetchContent)
    FetchContent_Declare(xsflib SOURCE_DIR ${XSFLIB_SOURCE_DIR})
    FetchContent_MakeAvailable(xsflib)
elseif (XSFLIB_ROUTE STREQUAL "find_package")
    find_package(xsflib 1.0 REQUIRED)
else()
    message(FATAL_ERROR "XSFLIB_ROUTE is ${XSFLIB_ROUTE}: use subdirectory, fetchcontent or find_package")
endif()

add_executable(package_test main.cpp)
target_link_libraries(package_test PRIVATE xsflib::psf xsflib::testing)
set_target_properties(package_test PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)

enable_testing()
add_test(NAME package_test COMMAND package_test)
