cmake_minimum_required(VERSION 3.16)
# Fail loudly instead of silently configuring a HOST build. See the root
# CMakeLists.txt for the full explanation; short version: a bad IDF_PATH makes
# the include() below fail, CMake carries on, and you get a wall of
# "unable to find library -lkernel32" that looks like a broken toolchain.
if(NOT EXISTS "$ENV{IDF_PATH}/tools/cmake/project.cmake")
    message(FATAL_ERROR "IDF_PATH ('$ENV{IDF_PATH}') is not an ESP-IDF checkout. Run export.ps1 first, or use build.ps1 / Ctrl+Shift+B.")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(step09_espressif_sensing)

# =====================================================================
#  esp-radar 0.3.4 does not compile at -O2. This fixes it.
# =====================================================================
#  The rest of this repo builds at -O2 (CONFIG_COMPILER_OPTIMIZATION_PERF,
#  see sdkconfig.defaults). GCC only runs its strict-aliasing analysis at
#  -O2 and above, so this never fires in Espressif's own -Og builds - but
#  here it does, fifteen times, as a hard error because ESP-IDF promotes
#  it with -Werror:
#
#    esp_radar.c:416: error: dereferencing type-punned pointer will break
#                     strict-aliasing rules [-Werror=strict-aliasing]
#
#  The code is genuinely type-punning: esp_radar.c reads the integer field
#  rx_ctrl->he_siga1 through (esp_wifi_htsig_t *), (esp_wifi_vht_siga1_t *)
#  and friends, to rebuild the classic mcs / cwb / stbc values that do not
#  exist as struct fields on a MAC-version-3 part like the ESP32-C5. It is
#  the same incompatibility this project's README is about, seen from
#  Espressif's side.
#
#  -fno-strict-aliasing, not -Wno-error=strict-aliasing: at -O2 the
#  optimiser is otherwise entitled to assume those accesses cannot alias
#  and may reorder or discard them. Silencing the diagnostic alone would
#  leave a real miscompilation risk; this tells GCC not to make the
#  assumption in the first place. Scoped to the one component.
idf_component_get_property(esp_radar_lib espressif__esp-radar COMPONENT_LIB)
target_compile_options(${esp_radar_lib} PRIVATE -fno-strict-aliasing)
