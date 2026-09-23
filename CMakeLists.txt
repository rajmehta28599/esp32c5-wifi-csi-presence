# =====================================================================
#  Root project: THE FINAL HUMAN DETECTOR
# =====================================================================
#  This makes D:\ESP32C5_CSI itself a valid ESP-IDF project, so opening
#  the repo folder in Antigravity / VS Code and pressing the extension's
#  Build button Just Works. It builds the SAME firmware as
#  step08_final_human_detector, reusing that folder's sources directly -
#  there is no copy of the code here, so the two can never drift apart.
#
#  The eight step folders remain independent projects. Use them to walk
#  through the tutorial; use this one when you just want the finished
#  detector.
#
#  Build dir here is D:\ESP32C5_CSI\build - separate from each step's
#  own build dir, so nothing collides.
# =====================================================================
cmake_minimum_required(VERSION 3.16)

# Pull in step08's "main" component instead of duplicating it.
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/step08_final_human_detector/main")

# ---------------------------------------------------------------------
#  Fail loudly if IDF_PATH is wrong, instead of silently building for the
#  host. Without this guard, a bad IDF_PATH makes the include() below fail,
#  CMake carries on regardless, project() picks the HOST compiler, and the
#  visible error is a wall of:
#      ld.lld: error: unable to find library -lkernel32
#      The C compiler ... is not able to compile a simple test program
#  which looks like a broken toolchain and is nothing of the kind.
#  Seen on 2026-09-07 when the IDE resolved no ESP-IDF setup and fell back
#  to %USERPROFILE%\esp\esp-idf, which does not exist on this machine.
if(NOT DEFINED ENV{IDF_PATH})
    message(FATAL_ERROR
        "IDF_PATH is not set.\n"
        "  Command line : run  C:\\esp\\v6.0.2\\esp-idf\\export.ps1  first "
        "(or use .\\build.ps1, which does it for you).\n"
        "  IDE          : the ESP-IDF extension resolved no setup. Use its "
        "setup picker, or press Ctrl+Shift+B, which does not depend on the "
        "extension's configuration at all.")
endif()
if(NOT EXISTS "$ENV{IDF_PATH}/tools/cmake/project.cmake")
    message(FATAL_ERROR
        "IDF_PATH points at '$ENV{IDF_PATH}', which is not an ESP-IDF "
        "checkout (no tools/cmake/project.cmake there).\n"
        "  If that path ends in \\esp\\esp-idf with no version number, it is "
        "the ESP-IDF extension's fallback value, which means it resolved no "
        "setup - see docs/REFERENCES.md and .vscode/settings.json.\n"
        "  Installed here: C:\\esp\\v6.0.2\\esp-idf  and  D:\\esp\\v6.0.2\\esp-idf")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32c5_csi_human_detector)
