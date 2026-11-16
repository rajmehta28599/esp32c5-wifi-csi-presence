<#
  run_tests.ps1 - build and run Step 10's host unit tests with MSVC.

  Usage (from anywhere):
      powershell -ExecutionPolicy Bypass -File step10_recognise_count_vitals\test\run_tests.ps1
      ... run_tests.ps1 -Only vitals        # one test

  What these tests are: the analytics modules in ../main (csi_dsp, csi_ncc,
  vitals, gait_id, count_est, motion_gate, person_vitals, activity, csi_gain) contain no ESP-IDF
  code, so they compile on the PC. Each test feeds a SYNTHETIC signal with a
  known answer - a 15 breaths/min + 72 beats/min chest, three people with
  different gaits, one mover versus two, breathing versus walking for the
  motion gate - and checks the module recovers it; test_pvitals instead
  drives person_vitals with event sequences (labels, counts, presence
  lapses, analyses) in main.c's order, and test_activity scripts the
  per-packet decisions activity.c reads. test_e2e drives all of them through
  a copy of main.c's front end (normalisation, EMAs, Step 08's motion
  metric). main.c itself is NOT compiled here: it needs ESP-IDF.

  What they are NOT: evidence that any of it works on a real person in a
  real room. That needs the board, and a reference (a chest strap or pulse
  oximeter for heart rate), as described in the README under Step 10.

  Compiled with /W4 /WX: a warning here is a failure, the same bar the
  ESP-IDF build holds the firmware to.
#>
param(
    [string]$Only = ""
)

$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$main = (Resolve-Path (Join-Path $here '..\main')).Path
$out  = Join-Path $here 'bin'
New-Item -ItemType Directory -Force $out | Out-Null

# test name -> module sources it links against
$tests = [ordered]@{
    'dsp'    = @('csi_dsp.c')
    'ncc'    = @('csi_ncc.c')
    'vitals' = @('vitals.c', 'csi_dsp.c')
    'gait'   = @('gait_id.c', 'csi_dsp.c')
    'count'  = @('count_est.c', 'csi_dsp.c')
    'gate'   = @('motion_gate.c', 'csi_dsp.c')
    # event sequences only: vitals.h is used for the vitals_out_t type
    'pvitals' = @('person_vitals.c')
    # scripted per-packet decisions (present, gate, hold): no CSI at all
    'activity' = @('activity.c')
    # the rx_ctrl gain bytes and the per-row summary (D-176): no CSI at all
    'gain'   = @('csi_gain.c')
    # all modules together, driven the way main.c drives them
    'e2e'    = @('csi_dsp.c', 'csi_ncc.c', 'vitals.c', 'gait_id.c', 'count_est.c', 'motion_gate.c', 'person_vitals.c', 'activity.c')
}

# vcvars64.bat calls vswhere.exe by bare name; it lives here, not on PATH.
$vsInstaller = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer'
$vswhere = Join-Path $vsInstaller 'vswhere.exe'
if (-not (Test-Path $vswhere)) { Write-Error "vswhere.exe not found - install Visual Studio 2022 (Desktop C++)" }
$vs = & $vswhere -latest -products * -property installationPath
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path $vcvars)) { Write-Error "vcvars64.bat not found under $vs" }
$env:PATH = "$vsInstaller;$env:PATH"

$failed = @()
foreach ($name in $tests.Keys) {
    if ($Only -and $name -ne $Only) { continue }
    $src = Join-Path $here "test_$name.c"
    if (-not (Test-Path $src)) { Write-Host "[skip] test_$name.c not written yet" -ForegroundColor DarkYellow; continue }
    $mods = ($tests[$name] | ForEach-Object { "`"$(Join-Path $main $_)`"" }) -join ' '
    # Each test gets its own object directory: csi_dsp.obj is compiled for
    # several tests, and a shared bin\ let two concurrent runs clobber it.
    $obj  = Join-Path $out "obj_$name"
    New-Item -ItemType Directory -Force $obj | Out-Null
    $exe  = Join-Path $out "test_$name.exe"
    # /std:c11 so MSVC takes the same C the firmware is written in;
    # /wd4996: MSVC's "use strcpy_s" nag, not a real portability issue.
    $cl = "cl /nologo /std:c11 /W4 /WX /wd4996 /O2 /fp:precise /D_CRT_SECURE_NO_WARNINGS " +
          "/I`"$main`" /I`"$here`" `"$src`" $mods /Fe:`"$exe`" /Fo:`"$obj\\`""
    Write-Host "[build] test_$name" -ForegroundColor Cyan
    cmd /c "`"$vcvars`" >nul && $cl"
    if ($LASTEXITCODE -ne 0) { $failed += "$name (compile)"; continue }
    Write-Host "[run]   test_$name" -ForegroundColor Cyan
    & $exe
    if ($LASTEXITCODE -ne 0) { $failed += $name }
}

Write-Host ""
if ($failed.Count) {
    Write-Host "FAILED: $($failed -join ', ')" -ForegroundColor Red
    exit 1
}
Write-Host "ALL HOST TESTS PASSED" -ForegroundColor Green
exit 0
