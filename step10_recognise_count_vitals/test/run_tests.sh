#!/usr/bin/env bash
#
#  run_tests.sh - build and run Step 10's host unit tests with gcc (Linux).
#                 Bash mirror of run_tests.ps1 (which uses MSVC on Windows).
#
#  Usage (from anywhere):
#      bash step10_recognise_count_vitals/test/run_tests.sh
#      ... run_tests.sh --only vitals         # one test
#      ... run_tests.sh --out DIR             # where binaries go
#                                             # (default ${TMPDIR:-/tmp}/esp32c5_host_tests)
#      ... run_tests.sh --no-werror           # warnings reported, not fatal
#
#  What these tests are: the analytics modules in ../main (csi_dsp, csi_ncc,
#  vitals, gait_id, count_est, motion_gate, person_vitals, activity, csi_gain) contain no ESP-IDF
#  code, so they compile on the PC. Each test feeds a SYNTHETIC signal with a
#  known answer - a 15 breaths/min + 72 beats/min chest, three people with
#  different gaits, one mover versus two, breathing versus walking for the
#  motion gate - and checks the module recovers it; test_pvitals instead
#  drives person_vitals with event sequences (labels, counts, presence
#  lapses, analyses) in main.c's order, and test_activity scripts the
#  per-packet decisions activity.c reads. test_e2e drives all of them through
#  a copy of main.c's front end (normalisation, EMAs, Step 08's motion
#  metric). main.c itself is NOT compiled here: it needs ESP-IDF.
#
#  What they are NOT: evidence that any of it works on a real person in a
#  real room. That needs the board, and a reference (a chest strap or pulse
#  oximeter for heart rate), as described in the README under Step 10.
#
#  Compiled with -Wall -Wextra -Werror: a warning here is a failure, the
#  same bar the ESP-IDF build holds the firmware to. Binaries go OUTSIDE the
#  repo (the checkout is shared with Windows over NTFS), never test/bin.
#

set -u

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
main="$(cd "$here/../main" && pwd)"
out="${TMPDIR:-/tmp}/esp32c5_host_tests"
only=""
werror="-Werror"

while [ $# -gt 0 ]; do
    case "$1" in
        --only)      [ $# -ge 2 ] || { echo "--only needs a test name" >&2; exit 2; }; only="$2"; shift 2 ;;
        --only=*)    only="${1#--only=}"; shift ;;
        --out)       [ $# -ge 2 ] || { echo "--out needs a directory" >&2; exit 2; }; out="$2"; shift 2 ;;
        --out=*)     out="${1#--out=}"; shift ;;
        --no-werror) werror=""; shift ;;
        -h|--help)   sed -n '2,32p' "$0"; exit 0 ;;
        *) echo "unknown option: $1 (try --help)" >&2; exit 2 ;;
    esac
done

# resolve first, so "bin", "./x/../" or a symlink cannot slip past the check
out="$(realpath -m -- "$out")"
case "$out" in
    "$here"/*|"$here") echo "refusing --out inside the test folder: $out" >&2; exit 2 ;;
esac
mkdir -p "$out" || exit 1

# test name -> module sources it links against (same order as run_tests.ps1)
names=(dsp ncc vitals gait count gate pvitals activity gain e2e)
declare -A mods=(
    [dsp]="csi_dsp.c"
    [ncc]="csi_ncc.c"
    [vitals]="vitals.c csi_dsp.c"
    [gait]="gait_id.c csi_dsp.c"
    [count]="count_est.c csi_dsp.c"
    [gate]="motion_gate.c csi_dsp.c"
    # event sequences only: vitals.h is used for the vitals_out_t type
    [pvitals]="person_vitals.c"
    # scripted per-packet decisions (present, gate, hold): no CSI at all
    [activity]="activity.c"
    # the rx_ctrl gain bytes and the per-row summary (D-176): no CSI at all
    [gain]="csi_gain.c"
    # all modules together, driven the way main.c drives them
    [e2e]="csi_dsp.c csi_ncc.c vitals.c gait_id.c count_est.c motion_gate.c person_vitals.c activity.c"
)

if [ -n "$only" ] && [ -z "${mods[$only]+x}" ]; then
    echo "unknown test '$only' (one of: ${names[*]})" >&2; exit 2
fi

[ -z "$werror" ] && echo "[note]  built WITHOUT -Werror: warnings are not failures in this run"

failed=()
for name in "${names[@]}"; do
    if [ -n "$only" ] && [ "$name" != "$only" ]; then continue; fi
    src="$here/test_$name.c"
    if [ ! -f "$src" ]; then echo "[skip] test_$name.c not written yet"; continue; fi
    # Each test gets its own directory: csi_dsp.c is compiled for several
    # tests, and a shared one would let two concurrent runs clobber it.
    dir="$out/obj_$name"
    mkdir -p "$dir" || exit 1
    exe="$dir/test_$name"
    srcs=()
    for m in ${mods[$name]}; do srcs+=("$main/$m"); done
    echo "[build] test_$name"
    # -std=c11: the same C the firmware is written in. No -ffast-math.
    if ! gcc -std=c11 -O2 -Wall -Wextra $werror -I "$main" -I "$here" \
            "$src" "${srcs[@]}" -lm -o "$exe"; then
        failed+=("$name (compile)"); continue
    fi
    echo "[run]   test_$name"
    if ! "$exe"; then failed+=("$name"); fi
done

echo ""
if [ ${#failed[@]} -gt 0 ]; then
    echo "FAILED: $(IFS=,; echo "${failed[*]}" | sed 's/,/, /g')"
    exit 1
fi
echo "ALL HOST TESTS PASSED"
exit 0
