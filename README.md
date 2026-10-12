# xsflib

xsflib is a C++20 library for players and rippers of the PSF family of formats, often called xSF. It holds the parts
that don't depend on a console: the PSF container and its tags, playback with length, fade and seeking, the frames of
command-line play and rip tools, and a foobar2000 input component.

Each module is a static library. A project links the ones it uses.

| Module | What it holds |
|---|---|
| `xsflib::common` | Byte order, bounds-checked reads of binary files, ASCII and UTF-8 helpers, numbers as text, files and their UTF-8 paths, WAV files, SHA-1 and memory images |
| `xsflib::console` | A command-line tool's console: the arguments in UTF-8 on Windows, UTF-8 or line-buffered output, and a window that waits before it closes |
| `xsflib::psf_container` | The PSF container without zlib: the header, the tags and the times in them. A reader passes its decompressor |
| `xsflib::psf` | The PSF container with zlib's decompressor and writer. Also the programs and sets of the formats that copy a set's files into their programs, and of the formats that hold each file once |
| `xsflib::player` | A template of playback with length, fade and seeking on a format's player. Also the parts that every format's player shares, and rendering ahead on a worker thread |
| `xsflib::cli` | WAV and FLAC output and the frames of a format's play tool and rip tool. It links `xsflib::console` |
| `xsflib::rip` | The names of a rip's files, functions for rip tools, and the length analysis that times a sequence on a model of a game's sound code |

`include/xsflib/foobar2000/input.h` is a foobar2000 input component as a template on a format's traits.
`xsflib::testing` holds checks for unit tests, a channel log, and helpers for the tests of a format's sets
(`testing/sets.h`). The log records a sound's output and its channels. Two logs of one sound can be compared line by
line.

`cmake/XsflibApps.cmake` builds common parts of a program: the Windows version resource, the macOS droplet apps and the
foobar2000 component. A project includes `cmake/XsflibProject.cmake` from its submodule before it adds the libraries.
`xsflib_project` sets the C++ standard, a release build by default, the static C runtime and the compiler options that
keep the output the same on both platforms. With clang-cl and a single-configuration generator (Ninja, NMake), a release
build requires `-DCMAKE_BUILD_TYPE=Release`. CMake sets `CMAKE_BUILD_TYPE` to Debug there before xsflib can set
Release. `xsflib_add_libraries` adds the libraries from the project's submodules. `cmake/XsflibPresets.json` has
`release` and `debug` presets for configuring, building and testing. A project's `CMakePresets.json` includes it from
the submodule. The include requires presets version 4. That version requires CMake 3.23 or later.

`ci/` holds composite actions for a project's GitHub workflows: building and testing, the steps of a release, and the
foobar2000 SDK and component. A workflow checks the project out with its submodules, then uses an action by its path in
the checkout, such as `uses: ./externals/xsflib/ci/build-and-test`.

## Building

xsflib requires CMake 3.20 or later and a C++20 compiler. The tests' checks come from seqlib, a Git submodule at
`externals/seqlib`. Clone with `git clone --recurse-submodules`, or run `git submodule update --init` in an existing
clone. Without the submodule, the build requires an installed seqlib.

xsflib supports macOS on Apple Silicon and Windows x64 built with clang-cl. Other platforms and compilers aren't
supported.

- **Windows x64**: Visual Studio 2022 with the C++ workload and its "C++ Clang tools for Windows" component. Configure
  with `cmake -S . -B build -A x64 -T ClangCL` in place of the first command below.
- **macOS on Apple Silicon**: Xcode's command-line tools (`xcode-select --install`) and CMake (from cmake.org or
  Homebrew).

```sh
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release
```

The PSF container requires zlib, and FLAC output requires libFLAC. An installed copy of each library is used if CMake
finds one. Otherwise CMake downloads and builds it.

Output is meant to be the same on both platforms. xsflib turns off the contraction of floating-point operations into
fused multiply-adds. A project that includes xsflib's headers should build its sources the same way.

Compressed data is the exception. zlib and libFLAC compress differently from one build to another. A system's copy may
differ from the one that CMake downloads. The decompressed programs and the audio are the same.

## Options

| Option | Default | Effect |
|---|---|---|
| `XSFLIB_PSF` | on when built alone | `xsflib::psf` and the player framework. Requires zlib. |
| `XSFLIB_CLI` | on when built alone | `xsflib::cli`, WAV and FLAC output. Requires libFLAC. |
| `XSFLIB_ZSTD` | off | Zstandard alone, as `xsflib::zstd`. |
| `XSFLIB_DISC_LIBRARIES` | off | zlib, Zstandard, liblzma and bzip2, as `xsflib::zlib`, `xsflib::zstd`, `xsflib::lzma` and `xsflib::bzip2`, for a library that reads disc images. |
| `XSFLIB_TESTS` | on when built alone | The tests, registered with CTest. |
| `XSFLIB_INSTALL` | off | Installs the headers, the libraries and the CMake package. Requires installed copies of the libraries above. |

## Using xsflib

Each module is a static library with an alias, such as `xsflib::psf`. A project links the modules it uses. xsflib also
provides the libraries it uses as `xsflib::zlib`, `xsflib::zstd`, `xsflib::lzma`, `xsflib::bzip2` and `xsflib::flac`,
and the checks for unit tests as `xsflib::testing`. `xsflib::testing` uses seqlib's checks. A project that uses seqlib
adds it before xsflib. xsflib then uses that copy. Otherwise xsflib adds the copy in its submodule `externals/seqlib`,
and builds only what a target links.

Below another project, the options are off by default. A project turns on the ones it uses before it adds xsflib.

As a Git submodule:

```cmake
set(XSFLIB_PSF ON)
add_subdirectory(externals/xsflib EXCLUDE_FROM_ALL)
target_link_libraries(my_player PRIVATE xsflib::psf)
```

With FetchContent:

```cmake
include(FetchContent)
FetchContent_Declare(xsflib GIT_REPOSITORY <the repository's URL> GIT_TAG <a release tag>)
set(XSFLIB_PSF ON)
FetchContent_MakeAvailable(xsflib)
target_link_libraries(my_player PRIVATE xsflib::psf)
```

From an installed copy (`cmake --install build` after configuring with `-DXSFLIB_INSTALL=ON`):

```cmake
find_package(xsflib 1.0 REQUIRED)
target_link_libraries(my_player PRIVATE xsflib::psf)
```

## Licence

xsflib is under the MIT licence (`LICENSE`). `THIRD-PARTY-NOTICES.md` lists the libraries it uses and their licences.
