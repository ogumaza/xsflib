# SPDX-License-Identifier: MIT

# The parts of a program's build that the projects using xsflib share: the Windows version resource and UTF-8 code
# page, the macOS droplet apps, a sequence converter's tests, the foobar2000 component, the XMPlay plugin, and the hook
# for locally supplied tests. The functions are available to a project once it has added xsflib.

# Gives `target` the version `VERSION`. The target's sources get the version as the string macro `DEFINE`, if given. On
# Windows, the target also gets the version resource that Explorer shows in the file's properties, with DESCRIPTION,
# PRODUCT and COPYRIGHT.
#
#   xsflib_add_version(xyzplay VERSION ${xyz_VERSION} DEFINE XYZ_VERSION DESCRIPTION "xyz renderer" PRODUCT xyz
#                      COPYRIGHT "Copyright (c) 2026 the xyz authors. MIT licence.")
function(xsflib_add_version target)
    cmake_parse_arguments(arg "" "VERSION;DEFINE;DESCRIPTION;PRODUCT;COPYRIGHT" "" ${ARGN})
    if (arg_DEFINE)
        target_compile_definitions(${target} PRIVATE ${arg_DEFINE}="${arg_VERSION}")
    endif()

    if (WIN32)
        get_target_property(type ${target} TYPE)
        if (type STREQUAL "EXECUTABLE")
            set(VERSION_FILE_TYPE VFT_APP)
            set(VERSION_FILE_NAME ${target}${CMAKE_EXECUTABLE_SUFFIX})
        else()
            set(VERSION_FILE_TYPE VFT_DLL)
            set(VERSION_FILE_NAME ${target}${CMAKE_SHARED_LIBRARY_SUFFIX})
        endif()
        string(REGEX MATCH "^([0-9]+)\\.([0-9]+)\\.([0-9]+)" version_parts ${arg_VERSION})
        set(VERSION ${arg_VERSION})
        set(VERSION_MAJOR ${CMAKE_MATCH_1})
        set(VERSION_MINOR ${CMAKE_MATCH_2})
        set(VERSION_PATCH ${CMAKE_MATCH_3})
        set(VERSION_DESCRIPTION ${arg_DESCRIPTION})
        set(VERSION_NAME ${target})
        set(VERSION_PRODUCT ${arg_PRODUCT})
        set(VERSION_COPYRIGHT ${arg_COPYRIGHT})
        set(rc ${CMAKE_CURRENT_BINARY_DIR}/${target}_version.rc)
        configure_file(${CMAKE_CURRENT_FUNCTION_LIST_DIR}/templates/version.rc.in ${rc} @ONLY)
        target_sources(${target} PRIVATE ${rc})
    endif()
endfunction()

# Windows: makes UTF-8 the code page of the command-line tool `target`. Without it, the tool can't open paths outside
# the system's ANSI code page. Files dropped on the tools can have such paths. The linker merges the manifest into the
# one that the linker creates.
function(xsflib_add_utf8_manifest target)
    if (MSVC)
        target_sources(${target} PRIVATE ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/templates/utf8.manifest)
    endif()
endfunction()

# macOS: a droplet app for the command-line tool `tool`. Files dropped on the app in the Finder go to the tool. The
# app is an AppleScript applet with the tool inside, in Contents/Resources. The app's Info.plist holds VERSION. The app
# is APP_NAME.app (by default, tool.app). The app's script is SCRIPT. By default, it's the rip and play tools' droplet
# script with `tool` filled in. RESOURCES are files that go into Contents/Resources beside the tool, such as the
# licences that a package ships. A relative path is relative to the current source folder.
function(xsflib_add_droplet tool)
    cmake_parse_arguments(arg "" "VERSION;SCRIPT;APP_NAME" "RESOURCES" ${ARGN})
    if (NOT arg_APP_NAME)
        set(arg_APP_NAME ${tool})
    endif()
    if (NOT APPLE)
        return()
    endif()

    find_program(OSACOMPILE osacompile)
    find_program(CODESIGN codesign)
    find_program(PLUTIL plutil)
    if (NOT OSACOMPILE OR NOT CODESIGN OR NOT PLUTIL)
        return()
    endif()

    set(droplet_app ${CMAKE_BINARY_DIR}/${arg_APP_NAME}.app)
    if (arg_SCRIPT)
        set(droplet_script ${arg_SCRIPT})
    else()
        set(DROPLET_TOOL ${tool})
        set(droplet_script ${CMAKE_BINARY_DIR}/${tool}.applescript)
        configure_file(${CMAKE_CURRENT_FUNCTION_LIST_DIR}/templates/droplet.applescript.in ${droplet_script} @ONLY)
    endif()
    set(resources)
    foreach (resource IN LISTS arg_RESOURCES)
        get_filename_component(resource ${resource} ABSOLUTE)
        list(APPEND resources ${resource})
    endforeach()
    set(copy_resources)
    if (resources)
        set(copy_resources COMMAND ${CMAKE_COMMAND} -E copy ${resources} ${droplet_app}/Contents/Resources/)
    endif()
    add_custom_command(
        OUTPUT ${droplet_app}/Contents/Resources/${tool}
        COMMAND ${CMAKE_COMMAND} -E rm -rf ${droplet_app}
        COMMAND ${OSACOMPILE} -o ${droplet_app} ${droplet_script}
        COMMAND ${PLUTIL} -replace CFBundleShortVersionString -string ${arg_VERSION} ${droplet_app}/Contents/Info.plist
        COMMAND ${PLUTIL} -replace CFBundleVersion -string ${arg_VERSION} ${droplet_app}/Contents/Info.plist
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${tool}> ${droplet_app}/Contents/Resources/${tool}
        ${copy_resources}
        COMMAND ${CODESIGN} --force --deep --sign - ${droplet_app}
        DEPENDS ${tool} ${droplet_script} ${resources}
        COMMENT "Building ${arg_APP_NAME}.app"
        VERBATIM)
    add_custom_target(${tool}_app ALL DEPENDS ${droplet_app}/Contents/Resources/${tool})
endfunction()

# macOS: a droplet app that runs the command-line tool `tool` on the files dropped on it, one process for all of them.
# The app shows one line of results for each file. DESCRIPTION is the script's opening comment, and PROMPT the prompt of
# the file chooser. With TRUNCATED, the result line notes a truncated archive. With GROUPS, it notes an archive whose
# group files are missing. The app is APP_NAME.app, with VERSION in its Info.plist. RESOURCES go into the app beside the
# tool (see xsflib_add_droplet).
#
#   xsflib_add_droplet_tool(xyz2midi APP_NAME Xyz2midi VERSION ${PROJECT_VERSION}
#                           DESCRIPTION "Drop xyz archives (.xyz) here to convert them to MIDI, SoundFont and SFZ."
#                           PROMPT "Choose xyz archives (.xyz) to convert:" TRUNCATED GROUPS)
function(xsflib_add_droplet_tool tool)
    cmake_parse_arguments(arg "TRUNCATED;GROUPS" "APP_NAME;VERSION;DESCRIPTION;PROMPT" "RESOURCES" ${ARGN})
    if (NOT APPLE)
        return()
    endif()

    # The template marks the lines of each note with a comment at their end. Keep a note's lines without the mark, or
    # drop them. The lines that both notes share go with either.
    file(READ ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/templates/droplet_tool.applescript.in script)
    set(keep_truncated ${arg_TRUNCATED})
    set(keep_groups ${arg_GROUPS})
    if (arg_TRUNCATED OR arg_GROUPS)
        set(keep_notes ON)
    else()
        set(keep_notes OFF)
    endif()
    foreach (note truncated groups notes)
        if (keep_${note})
            string(REPLACE " --[${note}]" "" script "${script}")
        else()
            string(REGEX REPLACE "[^\n]* --\\[${note}\\]\n" "" script "${script}")
        endif()
    endforeach()

    set(TOOL ${tool})
    set(TOOL_APP ${arg_APP_NAME})
    set(TOOL_DESCRIPTION ${arg_DESCRIPTION})
    set(TOOL_PROMPT ${arg_PROMPT})
    string(CONFIGURE "${script}" script @ONLY)
    set(droplet_script ${CMAKE_BINARY_DIR}/${tool}.applescript)
    file(CONFIGURE OUTPUT ${droplet_script} CONTENT "${script}" @ONLY)
    xsflib_add_droplet(${tool} VERSION ${arg_VERSION} SCRIPT ${droplet_script} APP_NAME ${arg_APP_NAME}
                       RESOURCES ${arg_RESOURCES})
endfunction()

# The tests of the sequence converter `converter`. <converter>_version checks that the converter reports VERSION.
# <converter>_var_range checks that the converter rejects --var 32=1, past the player and global variables 0 to 31. The
# test program <converter>_test, from tests/<converter>_test.cpp and linked with LIBRARIES, converts and checks every
# sequence of an archive. <converter>_archive runs it on the archive that the cache variable <CONVERTER>_TEST_ARCHIVE
# names. EXTENSION is the archives' file extension.
#
#   xsflib_add_converter_tests(xyz2midi EXTENSION .xyz VERSION ${PROJECT_VERSION} LIBRARIES xyz2midi_core)
function(xsflib_add_converter_tests converter)
    cmake_parse_arguments(arg "" "EXTENSION;VERSION" "LIBRARIES" ${ARGN})
    string(TOUPPER ${converter} upper)

    add_executable(${converter}_test ${PROJECT_SOURCE_DIR}/tests/${converter}_test.cpp)
    target_link_libraries(${converter}_test PRIVATE ${arg_LIBRARIES})

    string(REPLACE "." "\\." version_pattern "${arg_VERSION}")
    add_test(NAME ${converter}_version COMMAND ${converter} --version)
    set_tests_properties(${converter}_version PROPERTIES PASS_REGULAR_EXPRESSION
                                                        "^${converter} ${version_pattern}[\r\n]*$")

    add_test(NAME ${converter}_var_range COMMAND ${converter} --var 32=1 missing${arg_EXTENSION})
    set_tests_properties(${converter}_var_range PROPERTIES PASS_REGULAR_EXPRESSION "--var 32=1 is out of range")

    set(${upper}_TEST_ARCHIVE "" CACHE FILEPATH "Sound archive (${arg_EXTENSION}) for the conversion test")
    if (${upper}_TEST_ARCHIVE AND EXISTS "${${upper}_TEST_ARCHIVE}")
        add_test(NAME ${converter}_archive COMMAND ${converter}_test "${${upper}_TEST_ARCHIVE}")
    endif()
endfunction()

# The foobar2000 component `name` (Windows with clang-cl, or macOS), built from SOURCES against the SDK in SDK
# and linked with LIBRARIES. It's a DLL on Windows and a .component bundle on macOS. OPTIONS are the compile options of
# the component's sources. BUNDLE_ID is the macOS bundle identifier, and VERSION the bundle's version.
#
# The SDK's Visual Studio and Xcode projects aren't used. The parts that a component requires (pfc, the SDK and the
# component client) are compiled here. On Windows the SDK's prebuilt shared.dll import library is linked. On macOS,
# shared is compiled too, from the files that its Xcode project builds. Those include Objective-C++ files. The caller
# enables OBJCXX for them first, at file scope: CMake doesn't allow enable_language in a function.
function(xsflib_add_foobar2000_component name)
    cmake_parse_arguments(arg "" "SDK;VERSION;BUNDLE_ID" "SOURCES;LIBRARIES;OPTIONS" ${ARGN})
    if (NOT MSVC AND NOT APPLE)
        message(FATAL_ERROR "the foobar2000 component requires clang-cl on Windows, or macOS")
    endif()

    set(SDK ${arg_SDK})
    foreach (dir pfc foobar2000/SDK foobar2000/foobar2000_component_client foobar2000/shared)
        if (NOT EXISTS ${SDK}/${dir})
            message(FATAL_ERROR "FOOBAR2000_SDK_DIR (${SDK}) has no ${dir}: point it at the unpacked SDK")
        endif()
    endforeach()

    # The SDK's libraries, built once however many components a build has.
    if (NOT TARGET fb2k_sdk)
        file(GLOB pfc_sources ${SDK}/pfc/*.cpp)
        file(GLOB sdk_sources ${SDK}/foobar2000/SDK/*.cpp)
        if (APPLE)
            # The SDK's Xcode projects also build its Objective-C++ files, with ARC. They leave out these Windows files.
            if (NOT CMAKE_OBJCXX_COMPILER_LOADED)
                message(FATAL_ERROR "xsflib_add_foobar2000_component: on macOS, call enable_language(OBJCXX) at file "
                                    "scope first")
            endif()
            file(GLOB pfc_objcxx_sources ${SDK}/pfc/*.mm)
            file(GLOB sdk_objcxx_sources ${SDK}/foobar2000/SDK/*.mm)
            list(APPEND pfc_sources ${pfc_objcxx_sources})
            list(APPEND sdk_sources ${sdk_objcxx_sources})
            list(REMOVE_ITEM pfc_sources ${SDK}/pfc/stdafx.cpp ${SDK}/pfc/win-objects.cpp)
            list(REMOVE_ITEM sdk_sources ${SDK}/foobar2000/SDK/stdafx.cpp ${SDK}/foobar2000/SDK/cfg_var_legacy.cpp
                 ${SDK}/foobar2000/SDK/commandline.cpp ${SDK}/foobar2000/SDK/componentversion.cpp)
            set(sdk_options -w $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
        else()
            set(sdk_options /W0)
        endif()

        add_library(fb2k_pfc STATIC ${pfc_sources})
        # SYSTEM so that the SDK's headers don't raise warnings in the component's build.
        target_include_directories(fb2k_pfc SYSTEM PUBLIC ${SDK} ${SDK}/foobar2000)
        if (WIN32)
            target_compile_definitions(fb2k_pfc PUBLIC UNICODE _UNICODE NOMINMAX)
        endif()
        target_compile_options(fb2k_pfc PRIVATE ${sdk_options})

        add_library(fb2k_sdk STATIC ${sdk_sources})
        target_link_libraries(fb2k_sdk PUBLIC fb2k_pfc)
        target_compile_options(fb2k_sdk PRIVATE ${sdk_options})

        if (APPLE)
            # The files that the SDK's shared.xcodeproj builds. The rest of foobar2000/shared is for Windows.
            add_library(fb2k_shared STATIC
                ${SDK}/foobar2000/shared/audio_math.cpp
                ${SDK}/foobar2000/shared/shared-apple.mm
                ${SDK}/foobar2000/shared/shared-nix.cpp
                ${SDK}/foobar2000/shared/utf8.cpp
            )
            target_link_libraries(fb2k_shared PUBLIC fb2k_pfc)
            target_compile_options(fb2k_shared PRIVATE ${sdk_options})
        endif()
    endif()

    if (APPLE)
        add_library(${name} MODULE ${arg_SOURCES} ${SDK}/foobar2000/foobar2000_component_client/component_client.cpp)
        # The component client uses some of its parameters only with cfg_var_legacy. cfg_var_legacy is for Windows. Like
        # the rest of the SDK, the client is built without warnings.
        set_source_files_properties(${SDK}/foobar2000/foobar2000_component_client/component_client.cpp
                                    PROPERTIES COMPILE_OPTIONS -w)
        # The bundle identifier follows the SDK sample's identifier (com.foobar2000.foo-sample).
        set_target_properties(${name} PROPERTIES
            BUNDLE TRUE
            BUNDLE_EXTENSION component
            MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/templates/Info.plist.in
            MACOSX_BUNDLE_BUNDLE_NAME ${name}
            MACOSX_BUNDLE_GUI_IDENTIFIER ${arg_BUNDLE_ID}
            MACOSX_BUNDLE_SHORT_VERSION_STRING ${arg_VERSION}
            MACOSX_BUNDLE_BUNDLE_VERSION ${arg_VERSION}
        )
        target_link_libraries(${name} PRIVATE fb2k_sdk fb2k_shared ${arg_LIBRARIES} "-framework Cocoa")
        target_compile_options(${name} PRIVATE ${arg_OPTIONS})
        # foobar2000 loads every component into one process. dyld can bind an exported C++ symbol, such as an inline SDK
        # function, to another component's copy. The SDK's Xcode projects build with hidden symbols for that reason.
        # Here too, only the entry point is exported. Code that nothing uses is stripped.
        target_link_options(${name} PRIVATE -Wl,-exported_symbol,_foobar2000_get_interface -Wl,-dead_strip)
        # The linker signs only the executable. Signing the bundle ad hoc seals its Info.plist too.
        add_custom_command(TARGET ${name} POST_BUILD
                           COMMAND codesign --force --sign - $<TARGET_BUNDLE_DIR:${name}> VERBATIM)
    else()
        # The shared.dll import library for this architecture.
        if (CMAKE_SIZEOF_VOID_P EQUAL 8)
            set(fb2k_arch x64)
        else()
            set(fb2k_arch Win32)
        endif()
        find_library(FOOBAR2000_SHARED_LIB
            NAMES shared-${fb2k_arch} shared
            PATHS ${SDK}/foobar2000/shared ${SDK}/foobar2000/shared/${fb2k_arch}
            NO_DEFAULT_PATH REQUIRED)

        add_library(${name} SHARED ${arg_SOURCES} ${SDK}/foobar2000/foobar2000_component_client/component_client.cpp)
        target_link_libraries(${name} PRIVATE fb2k_sdk ${arg_LIBRARIES} ${FOOBAR2000_SHARED_LIB})
        # NOMINMAX comes with fb2k_pfc. WIN32_LEAN_AND_MEAN leaves parts of <windows.h> out. The SDK's headers work
        # without those parts. The definition doesn't reach the SDK's sources.
        target_compile_definitions(${name} PRIVATE WIN32_LEAN_AND_MEAN)
        # C4702 (unreachable code) comes from the SDK. The SDK's raw-decoding wrappers call
        # input_stubs::decode_run_raw. That function only throws. The optimizer reports the warning in the SDK's
        # headers. SYSTEM (/external:W0) doesn't reach it there.
        target_compile_options(${name} PRIVATE ${arg_OPTIONS} /wd4702)
    endif()
endfunction()

# The XMPlay input plugin `name`, built from SOURCES against the XMPlay plugin SDK in SDK and linked with LIBRARIES.
# XMPlay is a 32-bit Windows program. The plugin is therefore a 32-bit DLL, built with clang-cl. XMPlay's plugins are
# named xmp-*.dll, such as xmp-xyz.dll. OPTIONS are the compile options of the plugin's sources. The SDK's xmpin.def
# exports XMPIN_GetInterface by name, and templates/xmplay_options.rc holds the options page of xsflib/xmplay/input.h.
#
#   xsflib_add_xmplay_plugin(xmp-xyz SDK ${XMPLAY_SDK_DIR} SOURCES xmp_xyz.cpp LIBRARIES xyz_core)
function(xsflib_add_xmplay_plugin name)
    cmake_parse_arguments(arg "" "SDK" "SOURCES;LIBRARIES;OPTIONS" ${ARGN})
    if (NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
        message(FATAL_ERROR "the XMPlay plugin requires clang-cl for 32-bit Windows (cmake -A Win32 -T ClangCL). "
                            "XMPlay is a 32-bit program.")
    endif()

    foreach (file xmpin.h xmpfunc.h xmpin.def)
        if (NOT EXISTS ${arg_SDK}/${file})
            message(FATAL_ERROR "XMPLAY_SDK_DIR (${arg_SDK}) has no ${file}: point it at the unpacked XMPlay plugin SDK")
        endif()
    endforeach()

    add_library(${name} SHARED ${arg_SOURCES} ${arg_SDK}/xmpin.def
                ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/templates/xmplay_options.rc)
    # SYSTEM so that the SDK's headers don't raise warnings in the plugin's build.
    target_include_directories(${name} SYSTEM PRIVATE ${arg_SDK})
    target_link_libraries(${name} PRIVATE ${arg_LIBRARIES})
    target_compile_definitions(${name} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(${name} PRIVATE ${arg_OPTIONS})
endfunction()

# Includes local_tests.cmake from each folder directly below the project's source folder. Those files register tests
# that use locally supplied files. xsflib_include_local_tests is a macro because the files must run in the project's
# scope.
macro(xsflib_include_local_tests)
    file(GLOB xsflib_local_tests CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/*/local_tests.cmake)
    foreach (xsflib_local_test IN LISTS xsflib_local_tests)
        include(${xsflib_local_test})
    endforeach()
endmacro()
