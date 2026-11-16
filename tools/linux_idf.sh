#!/usr/bin/env bash
# =====================================================================
#  Linux (Ubuntu) side of the dual-boot setup - the counterpart of
#  build.ps1. See docs/ide/README.md for why it is shaped like this.
#
#    tools/linux_idf.sh setup [v6.1]        install ESP-IDF via EIM, seed
#                                           per-folder sdkconfigs, write
#                                           Antigravity Linux user settings
#    tools/linux_idf.sh build  <step|root>  idf.py build
#    tools/linux_idf.sh flash  <step|root>  idf.py flash (no monitor)
#    tools/linux_idf.sh monitor <step|root>
#    tools/linux_idf.sh flash-monitor <step|root>
#    tools/linux_idf.sh clean <step|root>   delete the Linux build dir and
#                                           its sdkconfig, re-seed, build
#    tools/linux_idf.sh erase <step|root>   erase-flash (wipes NVS baseline)
#    ESPPORT=/dev/ttyACM1 ... picks a port; default: first Espressif device
#    tools/linux_idf.sh menuconfig <step|root>
#    tools/linux_idf.sh idf <step|root> <idf.py args...>
#    tools/linux_idf.sh doctor
#
#  Linux builds never touch the NTFS tree's build/ or sdkconfig (those
#  belong to the Windows v6.0.2 install). Each project folder <P> builds
#  into  ~/esp-build<P>  with its own  ~/esp-build<P>/sdkconfig , seeded
#  once from <P>/sdkconfig so the Wi-Fi credentials carry over. This is
#  the same path the ESP-IDF extension uses via the user settings
#  "${env:HOME}/esp-build${workspaceFolder}".
# =====================================================================
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IDF_VER_DEFAULT="v6.1"
ESP_ROOT="$HOME/.espressif"
EIM_JSON="$ESP_ROOT/tools/eim_idf.json"
BUILD_ROOT="$HOME/esp-build"
SETTINGS="$HOME/.config/Antigravity IDE/User/settings.json"

die() { echo "ERROR: $*" >&2; exit 1; }

# Folder for a step name, "root", or a path.
proj_dir() {
    case "${1:-}" in
        ""|root|.) echo "$REPO" ;;
        /*) echo "$1" ;;
        *) [ -d "$REPO/$1" ] || die "no such step folder: $1"; echo "$REPO/$1" ;;
    esac
}

# IDF version selected in the EIM registry (falls back to the default).
idf_ver() {
    if [ -f "$EIM_JSON" ]; then
        python3 -I - "$EIM_JSON" <<'PY'
import json, sys
d = json.load(open(sys.argv[1]))
sel = d.get("idfSelectedId")
for i in d.get("idfInstalled", []):
    if i.get("id") == sel:
        print(i.get("name", "")); break
PY
    fi
}

activate() {
    local ver; ver="$(idf_ver)"; ver="${ver:-$IDF_VER_DEFAULT}"
    local act="$ESP_ROOT/tools/activate_idf_${ver}.sh"
    [ -f "$act" ] || die "$act missing - run: tools/linux_idf.sh setup"
    # Sourcing it from a script fails: it tests ${0##*/} to decide whether
    # it was sourced, and here $0 is linux_idf.sh. Its -e mode prints
    # KEY=VALUE lines instead; export those.
    # Its PATH holds only the IDF tools (cmake comes from the system), so
    # append the caller's PATH.
    local line sys_path="$PATH"
    while IFS= read -r line; do
        case "$line" in [A-Za-z_]*=*) export "${line?}" ;; esac
    done < <(bash "$act" -e)
    export PATH="$PATH:$sys_path"
    command -v idf.py >/dev/null || die "idf.py not on PATH after $act -e"
}

port() {               # $ESPPORT wins unless empty or "auto" (tasks.json)
    if [ -n "${ESPPORT:-}" ] && [ "$ESPPORT" != auto ]; then echo "$ESPPORT"; return; fi
    local p
    p="$(ls /dev/serial/by-id/usb-Espressif_* 2>/dev/null | head -n1 || true)"
    [ -n "$p" ] || die "no Espressif USB device under /dev/serial/by-id"
    echo "$p"
}

seed_sdkconfig() {   # $1 = project dir
    local b="$BUILD_ROOT$1"
    mkdir -p "$b"
    if [ ! -f "$b/sdkconfig" ] && [ -f "$1/sdkconfig" ]; then
        cp "$1/sdkconfig" "$b/sdkconfig"
        echo "seeded $b/sdkconfig from $1/sdkconfig"
    fi
}

run_idf() {          # $1 = project dir, rest = idf.py args
    local p="$1"; shift
    seed_sdkconfig "$p"
    activate
    # Step 09's esp_csi_gain_ctrl ships prebuilt libs per IDF version and
    # has none for 6.1 (2026-10-12: ninja "libesp_csi_gain_ctrl.a ...
    # missing"); a build also rewrites the shared dependencies.lock that
    # the Windows v6.0.2 build uses. Refuse rather than touch it.
    if [ "$(basename "$p")" = step09_espressif_sensing ] && [ "${ESP_IDF_VERSION:-}" != 6.0 ]; then
        die "step09 needs ESP-IDF 6.0.x (selected: ${ESP_IDF_VERSION:-?}); build it on Windows"
    fi
    cd "$p"
    idf.py -B "$BUILD_ROOT$p" -D SDKCONFIG="$BUILD_ROOT$p/sdkconfig" "$@"
}

cmd_setup() {
    local ver="${1:-$IDF_VER_DEFAULT}"
    local missing=()
    for t in git wget flex bison cmake python3; do
        command -v "$t" >/dev/null || missing+=("$t")
    done
    command -v eim >/dev/null || missing+=("eim")
    if [ ${#missing[@]} -gt 0 ]; then
        echo "Missing: ${missing[*]}  - install them with the sudo line in docs/ide/README.md" >&2
        exit 1
    fi
    # Not needed for an ordinary build; EIM's check insists on them, so
    # skip that check when they are absent and say so.
    local skip=false
    for t in gperf ccache dfu-util; do
        command -v "$t" >/dev/null || { echo "note: $t not installed (optional)"; skip=true; }
    done

    if [ -f "$ESP_ROOT/tools/activate_idf_${ver}.sh" ]; then
        echo "ESP-IDF $ver already installed"
    else
        # esp32c5 only: the project builds nothing else, and "all" pulls
        # the xtensa toolchains too.
        # Run from ~/.espressif: EIM saves eim_config.toml in the cwd.
        mkdir -p "$ESP_ROOT"
        (cd "$ESP_ROOT" && eim install -i "$ver" -t esp32c5 -n true --skip-prerequisites-check "$skip")
    fi
    eim select "$ver" >/dev/null 2>&1 || true

    local idf_path="$ESP_ROOT/$ver/esp-idf"
    [ -d "$idf_path" ] || die "expected ESP-IDF at $idf_path"

    for d in "$REPO" "$REPO"/step*/; do seed_sdkconfig "${d%/}"; done

    mkdir -p "$(dirname "$SETTINGS")"
    local clangd; clangd="$(ls -d "$ESP_ROOT"/tools/esp-clangd/*/esp-clangd/bin/clangd 2>/dev/null | tail -n1 || true)"
    # esp-clangd ships no clang resource headers (stddef.h, stdarg.h);
    # esp-clang does. Without them every file shows 'stddef.h' not found.
    local resdir; resdir="$(ls -d "$ESP_ROOT"/tools/esp-clang/*/esp-clang/lib/clang/* 2>/dev/null | tail -n1 || true)"
    python3 -I - "$SETTINGS" "$idf_path" "$clangd" "$resdir" <<'PY'
import json, os, sys
path, idf, clangd, resdir = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
try:
    s = json.load(open(path))
except FileNotFoundError:
    s = {}
s.update({
    # Matches the "path" of the EIM registry entry (~/.espressif/tools/eim_idf.json).
    "idf.currentSetup": idf,
    # Off the NTFS tree, one dir per project folder; see tools/linux_idf.sh.
    "idf.buildPath": "${env:HOME}/esp-build${workspaceFolder}",
    "idf.sdkconfigFilePath": "${env:HOME}/esp-build${workspaceFolder}/sdkconfig",
    "idf.port": "detect",
    "idf.customExtraVars": {"IDF_CCACHE_ENABLE": "1"},
    # Espressif's clangd build (installed by EIM) knows the RISC-V ESP flags.
    **({"clangd.path": clangd} if clangd else {}),
    "clangd.arguments": [
        "--background-index",
        "--query-driver=${userHome}/.espressif/tools/**/bin/riscv32-esp-elf-*",
        "--compile-commands-dir=${userHome}/esp-build${workspaceFolder}",
    ] + ([f"--resource-dir={resdir}"] if resdir else []),
})
os.makedirs(os.path.dirname(path), exist_ok=True)
with open(path, "w") as f:
    json.dump(s, f, indent=4); f.write("\n")
print("wrote", path)
PY
    echo "Done. Run 'Developer: Reload Window' in the project window; the extension's Build button then uses $idf_path."
}

cmd_doctor() {
    echo "repo      : $REPO"
    echo "eim       : $(command -v eim || echo MISSING) $(eim --version 2>/dev/null | head -n1)"
    echo "selected  : $(idf_ver || true)"
    for t in gperf ccache dfu-util; do printf '%-10s: %s\n' "$t" "$(command -v $t || echo MISSING)"; done
    echo "port      : $( (port) 2>/dev/null || echo "none (board unplugged?)")"
    id -nG | grep -qw dialout && echo "dialout   : yes" || echo "dialout   : NO (sudo usermod -aG dialout $USER)"
    [ -f /etc/udev/rules.d/60-openocd.rules ] && echo "udev JTAG : yes" || echo "udev JTAG : missing (only needed for OpenOCD debugging)"
    for d in "$REPO" "$REPO"/step*/; do
        d="${d%/}"; local e; e="$(ls "$BUILD_ROOT$d"/*.elf 2>/dev/null | head -n1 || true)"
        printf '%-32s %s\n' "$(basename "$d")" "${e:+built: $(basename "$e")}"
    done
}

sub="${1:-}"; shift || true
case "$sub" in
    setup)      cmd_setup "$@" ;;
    doctor)     cmd_doctor ;;
    build)      run_idf "$(proj_dir "${1:-}")" build ;;
    menuconfig) run_idf "$(proj_dir "${1:-}")" menuconfig ;;
    clean)      p="$(proj_dir "${1:-}")"
                # The Windows sdkconfig in <P> is the seed and stays untouched.
                rm -rf "${BUILD_ROOT:?}$p"
                run_idf "$p" build ;;
    # Resolve the port in its own statement so a missing board stops here
    # (a failing $(...) inside an argument list would not trip set -e).
    flash)      p="$(proj_dir "${1:-}")"; pt="$(port)"; run_idf "$p" -p "$pt" flash ;;
    monitor)    p="$(proj_dir "${1:-}")"; pt="$(port)"; run_idf "$p" -p "$pt" monitor ;;
    flash-monitor) p="$(proj_dir "${1:-}")"; pt="$(port)"; run_idf "$p" -p "$pt" flash monitor ;;
    erase)      p="$(proj_dir "${1:-}")"; pt="$(port)"; run_idf "$p" -p "$pt" erase-flash ;;
    idf)        p="$(proj_dir "${1:-}")"; shift || true; run_idf "$p" "$@" ;;
    *) sed -n '2,25p' "$0"; exit 2 ;;
esac
