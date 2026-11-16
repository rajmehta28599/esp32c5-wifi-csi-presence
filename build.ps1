<#
  build.ps1 - one-command build/flash helper for the ESP32-C5 CSI project.

  Usage (from D:\ESP32C5_CSI):
      .\build.ps1 step01_hello                 # configure + build
      .\build.ps1 step03_csi_hello -Flash      # build + flash + monitor (auto port)
      .\build.ps1 step03_csi_hello -Flash -Port COM12
      .\build.ps1 step01_hello -Clean          # wipe build dir first
      .\build.ps1 -Doctor                      # diagnose the setup, build nothing

  It activates ESP-IDF v6.0.2 for you, so you do not have to remember the
  export.ps1 path. If you leave -Port off, it finds the board itself.
#>
param(
    [Parameter(Position = 0)]
    [string]$Step = "",
    [string]$Port = "",
    [switch]$Flash,
    [switch]$Monitor,
    [switch]$Clean,
    [switch]$Doctor
)

# ESP-IDF v6.0.2 - deliberately the SAME version the Antigravity ESP-IDF
# extension uses (its EIM registry selects C:\esp\v6.0.2\esp-idf),
# so the IDE and the command line can never disagree about sdkconfig
# format or the Wi-Fi API. All eight steps are build-verified on it.
$IDF_EXPORT = 'C:\esp\v6.0.2\esp-idf\export.ps1'
$ROOT       = $PSScriptRoot

# ===================================================================
#  Serial port discovery
# ===================================================================
# USB vendor IDs that actually appear on ESP32 devkits.
$UsbSerialVids = @{
    'VID_303A' = 'Espressif USB-Serial-JTAG (built into the C5)'
    'VID_10C4' = 'Silicon Labs CP210x bridge'
    'VID_1A86' = 'WCH CH34x bridge'
    'VID_0403' = 'FTDI bridge'
}

function Get-BoardPorts {
    <#  Returns real, currently-present USB serial ports, newest first.
        Bluetooth "Standard Serial over Bluetooth link" ports are excluded -
        they are always present on this machine and are never your board. #>
    $found = @()
    try {
        $devs = Get-CimInstance Win32_PnPEntity -ErrorAction Stop |
                Where-Object { $_.Name -match '\(COM\d+\)' -and $_.Present -ne $false }
        foreach ($d in $devs) {
            if ($d.Name -match 'Bluetooth') { continue }
            if ($d.Name -notmatch '\((COM\d+)\)') { continue }
            $com = $Matches[1]
            $why = 'USB serial device'
            foreach ($vid in $UsbSerialVids.Keys) {
                if ($d.DeviceID -like "*$vid*") { $why = $UsbSerialVids[$vid]; break }
            }
            $found += [pscustomobject]@{ Port = $com; Name = $d.Name; Why = $why }
        }
    } catch { }
    # The leading comma matters. Without it PowerShell unrolls a
    # single-element array on return, so with exactly ONE board plugged in
    # this hands back a bare PSCustomObject - whose .Count is $null in
    # Windows PowerShell 5.1, not 1. Every Count test below then fails and
    # the one-board case (the normal case) reports "no board found".
    return ,$found
}

function Show-NoBoardHelp {
    param([string]$Wanted)

    Write-Host ""
    Write-Host "==============================================================" -ForegroundColor Red
    Write-Host " NO ESP32-C5 FOUND" -ForegroundColor Red
    Write-Host "==============================================================" -ForegroundColor Red
    if ($Wanted) {
        Write-Host " You asked for $Wanted, but that port does not exist." -ForegroundColor Yellow
    }
    Write-Host ""
    Write-Host " Serial ports currently on this machine:" -ForegroundColor Yellow
    $all = @()
    try { $all = [System.IO.Ports.SerialPort]::GetPortNames() } catch { }
    if ($all.Count -eq 0) {
        Write-Host "   (none at all)"
    } else {
        foreach ($p in $all) {
            $desc = (Get-CimInstance Win32_SerialPort -ErrorAction SilentlyContinue |
                     Where-Object DeviceID -eq $p).Description
            if ($desc -match 'Bluetooth') {
                Write-Host "   $p  $desc   <- Bluetooth, NOT your board" -ForegroundColor DarkGray
            } else {
                Write-Host "   $p  $desc"
            }
        }
    }
    Write-Host ""
    Write-Host " Checklist:" -ForegroundColor Cyan
    Write-Host "   1. Is the board plugged in? A new COM port should appear"
    Write-Host "      within a second or two of connecting it."
    Write-Host "   2. Is it a DATA USB cable? Charge-only cables power the"
    Write-Host "      board's LED but create no COM port at all. This is the"
    Write-Host "      single most common cause."
    Write-Host "   3. Try the other USB socket on the board. Most C5 devkits"
    Write-Host "      have two: one wired to the USB-Serial-JTAG peripheral,"
    Write-Host "      one to a UART bridge chip."
    Write-Host "   4. If Windows shows an unknown device, install the bridge"
    Write-Host "      driver (CP210x from Silicon Labs, or CH34x from WCH)."
    Write-Host "   5. Force download mode: hold BOOT, tap RESET, release BOOT,"
    Write-Host "      then flash again."
    Write-Host ""
    Write-Host " Re-run '.\build.ps1 -Doctor' once it is connected." -ForegroundColor Cyan
    Write-Host ""
}

function Resolve-Port {
    param([string]$Requested)

    $ports = @(Get-BoardPorts)

    # Explicit port: verify it actually exists before we hand it to esptool,
    # otherwise you get a wall of CMake noise instead of a clear message.
    if ($Requested -and $Requested -ne 'auto') {
        $live = @()
        try { $live = [System.IO.Ports.SerialPort]::GetPortNames() } catch { }
        if ($live -contains $Requested) { return $Requested }
        Show-NoBoardHelp -Wanted $Requested
        return $null
    }

    # Auto-detect.
    if ($ports.Count -eq 1) {
        Write-Host "[build.ps1] Auto-detected board on $($ports[0].Port) - $($ports[0].Why)" -ForegroundColor Green
        return $ports[0].Port
    }
    if ($ports.Count -gt 1) {
        Write-Host "[build.ps1] Several USB serial ports are present:" -ForegroundColor Yellow
        foreach ($p in $ports) { Write-Host "    $($p.Port)  -  $($p.Why)" }
        Write-Host "[build.ps1] Re-run with the right one, e.g. -Port $($ports[0].Port)" -ForegroundColor Yellow
        return $null
    }
    Show-NoBoardHelp
    return $null
}

# ===================================================================
#  Doctor
# ===================================================================
if ($Doctor) {
    Write-Host ""
    Write-Host "=== ESP32-C5 CSI project doctor ===" -ForegroundColor Cyan
    Write-Host ""
    Write-Host "[1] ESP-IDF v6.0.2 export script"
    if (Test-Path $IDF_EXPORT) { Write-Host "    OK  $IDF_EXPORT" -ForegroundColor Green }
    else { Write-Host "    MISSING  $IDF_EXPORT" -ForegroundColor Red }

    Write-Host "[2] RISC-V toolchain (required for esp32c5)"
    $rv = 'C:\Espressif\tools\riscv32-esp-elf'
    if (Test-Path $rv) {
        Get-ChildItem $rv -Directory | ForEach-Object { Write-Host "    OK  $($_.Name)" -ForegroundColor Green }
    } else { Write-Host "    MISSING  $rv" -ForegroundColor Red }

    Write-Host "[3] Serial ports"
    $ports = @(Get-BoardPorts)
    if ($ports.Count -eq 0) {
        Write-Host "    No USB serial device found - the board is not connected." -ForegroundColor Red
        try {
            foreach ($p in [System.IO.Ports.SerialPort]::GetPortNames()) {
                $d = (Get-CimInstance Win32_SerialPort -ErrorAction SilentlyContinue | Where-Object DeviceID -eq $p).Description
                Write-Host "    (ignoring $p - $d)" -ForegroundColor DarkGray
            }
        } catch { }
    } else {
        foreach ($p in $ports) { Write-Host "    OK  $($p.Port)  -  $($p.Why)" -ForegroundColor Green }
    }

    Write-Host "[4] Built steps"
    # 'step0*' used to be the filter, which silently hid step10 onwards.
    # Match the naming pattern instead: stepNN_ with exactly two digits.
    Get-ChildItem $ROOT -Directory -Filter 'step*' |
            Where-Object { $_.Name -match '^step\d\d_' } | ForEach-Object {
        $bin = Join-Path $_.FullName "build\$($_.Name).bin"
        if (Test-Path $bin) { Write-Host "    OK       $($_.Name)" -ForegroundColor Green }
        else                { Write-Host "    unbuilt  $($_.Name)" -ForegroundColor DarkGray }
    }

    if ($ports.Count -gt 0) {
        Write-Host "[5] Chip identity on $($ports[0].Port)"
        & $IDF_EXPORT *> $null
        & esptool --port $ports[0].Port chip_id 2>&1 | Select-Object -Last 8
    } else {
        Write-Host "[5] Chip identity - skipped, no board" -ForegroundColor DarkGray
        Show-NoBoardHelp
    }
    exit 0
}

# ===================================================================
#  Normal build / flash
# ===================================================================
if (-not $Step) {
    Write-Error "No step given. Example:  .\build.ps1 step08_final_human_detector -Flash"
    exit 1
}
if (-not (Test-Path $IDF_EXPORT)) {
    Write-Error "ESP-IDF export.ps1 not found at $IDF_EXPORT - edit build.ps1"
    exit 1
}
$proj = Join-Path $ROOT $Step
if (-not (Test-Path $proj)) {
    Write-Error "No such step folder: $proj"
    exit 1
}

# Check the port BEFORE a long build, so you find out immediately.
$resolvedPort = ""
if ($Flash -or $Monitor) {
    $resolvedPort = Resolve-Port -Requested $Port
    if (-not $resolvedPort) {
        Write-Host "[build.ps1] Stopping before the build - fix the connection first." -ForegroundColor Red
        Write-Host "[build.ps1] To build without flashing:  .\build.ps1 $Step" -ForegroundColor Cyan
        exit 1
    }
}

Write-Host "[build.ps1] Activating ESP-IDF v6.0.2 ..." -ForegroundColor Cyan
& $IDF_EXPORT *> $null

Set-Location $proj

if ($Clean) {
    Write-Host "[build.ps1] Cleaning $Step ..." -ForegroundColor Yellow
    Remove-Item -Recurse -Force (Join-Path $proj 'build')   -ErrorAction SilentlyContinue
    Remove-Item -Force        (Join-Path $proj 'sdkconfig') -ErrorAction SilentlyContinue
}

Write-Host "[build.ps1] Target -> esp32c5" -ForegroundColor Cyan
if (-not (Test-Path (Join-Path $proj 'sdkconfig'))) {
    idf.py set-target esp32c5 | Out-Null
    if (-not $?) { Write-Error "set-target failed"; exit 1 }
}

$idfArgs = @()
if ($resolvedPort) { $idfArgs += @("-p", $resolvedPort) }

Write-Host "[build.ps1] Building $Step ..." -ForegroundColor Cyan
idf.py @idfArgs build
if (-not $?) { Write-Error "BUILD FAILED"; exit 1 }

if ($Flash) {
    Write-Host "[build.ps1] Flashing to $resolvedPort ..." -ForegroundColor Cyan
    idf.py @idfArgs flash monitor
} elseif ($Monitor) {
    idf.py @idfArgs monitor
}
