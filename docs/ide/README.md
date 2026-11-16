# IDE setup on a dual-boot machine (Windows + Ubuntu)

This repo is one folder on one NTFS partition. Windows opens it as `D:\ESP32C5_CSI` and Ubuntu as `/mnt/vol1/ESP32C5_CSI`. Both run Antigravity with the ESP-IDF extension (v2.3.0, the latest release as of 2026-10-12). The rules below let both sides work from the same checkout.

## The rule: tracked settings hold no machine paths

`idf.currentSetup`, `idf.customExtraVars`, `idf.port`, `idf.buildPath` and `idf.sdkconfigFilePath` have **Resource** scope in the extension's `package.json`. A folder's `.vscode/settings.json` therefore overrides the user settings for these keys.

Before 2026-10-12, the 11 tracked settings files carried `C:\` paths. On Linux, `idf.customExtraVars.IDF_PATH = C:\esp\...` won, and the extension could not find ESP-IDF. Those keys now live in each OS's **user** settings:

| OS | User settings file | Template |
|---|---|---|
| Windows | `%APPDATA%\Antigravity IDE\User\settings.json` | `windows-user-settings.jsonc` (the old tracked values; `idf.espIdfPath`, `idf.toolsPath` and `idf.pythonBinPath` are left out because extension v2.x no longer reads them) |
| Ubuntu | `~/.config/Antigravity IDE/User/settings.json` | written by `tools/linux_idf.sh setup` |

The original files are not in this repo. They are the `HEAD` versions in git.

## Each OS keeps its own build output and sdkconfig

| | Windows | Ubuntu |
|---|---|---|
| ESP-IDF | v6.0.2, `C:\esp\v6.0.2\esp-idf` | v6.1 via EIM, `~/.espressif/v6.1/esp-idf` |
| Build dir | `<folder>\build` | `~/esp-build/mnt/vol1/ESP32C5_CSI/<folder>` (ext4) |
| sdkconfig | `<folder>\sdkconfig` | `~/esp-build/.../<folder>/sdkconfig`, copied once from the Windows one so the Wi-Fi credentials carry over |
| Port | `COM13` | `detect` (the board appears as `/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_<MAC>-if00`, which links to `/dev/ttyACM0`) |

Why the Linux build and sdkconfig stay off the shared tree:

- **Different ESP-IDF versions.** A v6.1 build rewrites `sdkconfig` in v6.1 format. The verified Windows v6.0.2 builds (TESTING §5) would then read that file.
- **Build caches are per machine.** A Windows `build/` contains a `CMakeCache.txt` with `C:\` paths, so CMake on Linux would refuse it.
- **Disk space.** The NTFS partition was 96% full on 2026-10-12.

The extension expands `${workspaceFolder}` to the full folder path; `${workspaceFolderBasename}` is not supported (it also expands to the full path, `dist/extension.js` function `Qi`). That is why the Linux build path is `${env:HOME}/esp-build${workspaceFolder}`.

Changes to a credential made in a Linux menuconfig stay on Linux. Make the same change on Windows if you need it there.

## Step 09 does not build on v6.1

`espressif__esp_csi_gain_ctrl` ships prebuilt libraries per IDF version and has none for 6.1. A v6.1 build fails at link time. It also rewrites `step09_espressif_sensing/dependencies.lock` in the shared tree. `tools/linux_idf.sh` refuses to build Step 09 unless ESP-IDF 6.0.x is selected; build it on Windows. The extension's Build button has no such guard, so don't press it in the Step 09 folder on Linux.

## clangd

- Linux uses `esp-clangd` from EIM. It ships no clang resource headers, so the settings add `--resource-dir=<esp-clang>/lib/clang/<n>`; without it, every file reports `'stddef.h' file not found` (TESTING §5.2, 2026-10-12).
- An explicit `--compile-commands-dir` overrides `CompilationDatabase: build` in the step folders' `.clangd`. This was tested 2026-10-12 with clangd 23.1.0 `--check`.
- The Linux user settings point clangd at the Linux build dir.
- In a multi-root window (`ESP32C5_CSI.code-workspace`), vscode-clangd resolves `${workspaceFolder}` to the first folder only. Open a single step folder when you want accurate IntelliSense.

## Ubuntu: first-time setup

As done on 2026-10-12 (TESTING §5.2), without sudo:

```bash
# EIM CLI (official release binary) into ~/.local/bin
curl -fsSLO https://github.com/espressif/idf-im-ui/releases/download/v0.20.0/eim-cli-linux-x64.zip
unzip eim-cli-linux-x64.zip && install -m755 eim ~/.local/bin/eim

tools/linux_idf.sh setup v6.1     # eim install, sdkconfig copies, Antigravity user settings
tools/linux_idf.sh doctor
tools/linux_idf.sh build step10_recognise_count_vitals
```

`setup` skips EIM's prerequisite check when gperf, ccache or dfu-util is missing. An ordinary build does not need them; all 10 v6.1 builds passed without them.

Optional, needs sudo:

```bash
sudo apt-get install -y gperf ccache dfu-util
# OpenOCD / JTAG debugging over the C5's USB (EIM could not copy this without root):
sudo cp ~/.espressif/tools/openocd-esp32/*/openocd-esp32/share/openocd/contrib/60-openocd.rules /etc/udev/rules.d/
sudo udevadm control --reload && sudo udevadm trigger
```

`tools/linux_idf.sh flash` / `monitor` have **not yet been run on Linux** (2026-10-12; the board was unplugged at the time). The user is in the `dialout` group, which `/dev/ttyACM0` requires. To get updates through apt instead, use the signed repo in Espressif's EIM docs (`/etc/apt/sources.list.d/espressif.sources`, then `apt install eim-cli`).

## Tasks (Ctrl+Shift+B)

The seven ESP-IDF tasks in `.vscode/tasks.json` each have a `"linux"` override that runs `tools/linux_idf.sh`, so the same task names work on both OSes. The Windows commands are unchanged. On Linux, the port prompt's `auto` means the first `/dev/serial/by-id/usb-Espressif_*` device. **Clean rebuild** deletes only the Linux build folder and its `sdkconfig`, then copies the Windows `sdkconfig` again. The three Tools tasks are still Windows-only.

## Windows: once, after pulling this change

Paste `windows-user-settings.jsonc` into the user settings: Ctrl+Shift+P, then "Preferences: Open User Settings (JSON)". The command line (`build.ps1`, `export.ps1`) does not depend on any of this.
