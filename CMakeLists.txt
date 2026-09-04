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

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32c5_csi_human_detector)
