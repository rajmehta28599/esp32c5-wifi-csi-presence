cmake_minimum_required(VERSION 3.16)
# Fail loudly instead of silently configuring a HOST build. See the root
# CMakeLists.txt for the full explanation; short version: a bad IDF_PATH makes
# the include() below fail, CMake carries on, and you get a wall of
# "unable to find library -lkernel32" that looks like a broken toolchain.
if(NOT EXISTS "$ENV{IDF_PATH}/tools/cmake/project.cmake")
    message(FATAL_ERROR "IDF_PATH ('$ENV{IDF_PATH}') is not an ESP-IDF checkout. Run export.ps1 first, or use build.ps1 / Ctrl+Shift+B.")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(step04_csi_geometry)
