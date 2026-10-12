# SPDX-License-Identifier: MIT

# Installs the headers, the libraries and the CMake package that find_package(xsflib) reads.

if (XSFLIB_FETCHED_DEPENDENCIES)
    message(FATAL_ERROR "XSFLIB_INSTALL requires installed copies of the libraries that xsflib uses. These were "
                        "downloaded instead: ${XSFLIB_FETCHED_DEPENDENCIES}. Install them, or turn the options that "
                        "require them off.")
endif()

# The package's configuration finds the same libraries again, with the same options.
if (TARGET PkgConfig::XSFLIB_FLAC)
    set(XSFLIB_FLAC_FROM_PKG_CONFIG ON)
else()
    set(XSFLIB_FLAC_FROM_PKG_CONFIG OFF)
endif()
xsflib_install_package(xsflib)
