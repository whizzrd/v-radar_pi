# Building V-RADAR

Result: a `V-RADAR-<version>_<os>-<arch>.tar.gz` (+ `.sha256` and metadata `.xml`) that OpenCPN installs with
Options, Plugins, **Import plugin...** (see `INSTALL.md`).

## macOS (and Linux)

Needs an OpenCPN source tree for `include/ocpn_plugin.h` (default `~/src/github.com/OpenCPN/OpenCPN`, or pass
`-DOPENCPN_SOURCE_DIR=...`) and wxWidgets 3.2 (Homebrew on macOS).

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target tarball
```
The result is written to `build/`. The file targets the OpenCPN and wxWidgets ABI of the machine it is built on
(for example `darwin-wx32-15.7.3-x86_64`); Apple Silicon needs a build on an arm64 Mac.

## Windows

Requires the Windows support in `CMakeLists.txt` and `src/` (Winsock, `std::filesystem`, MSVC flags). The tarball is
named `..._msvc-wx32-10-x86`.

## Requirements

- Visual Studio 2022 (or Build Tools) with "Desktop development with C++", CMake >= 3.16, Git.
- **wxWidgets 3.2.x** for MSVC, DLL build (`wxMSW-3.2.x_vc14x_Dev` and `_ReleaseDLL` from wxwidgets.org). Unpack to one
  folder, headers from `wxWidgets-3.2.x-headers`. Set `WXWIN` to it; libs in `%WXWIN%\lib\vc14x_dll`.
- **OpenCPN headers**: an OpenCPN source tree (`include/ocpn_plugin.h`) at the release you target.
- **`opencpn.lib`**: the import library of `opencpn.exe`, which provides the plugin base class to the DLL.
  OpenCPN does not ship it; create it from the installed program (x86 Native Tools prompt):

  ```
  dumpbin /exports "C:\Program Files (x86)\OpenCPN\opencpn.exe" > exports.txt
  :: turn the export names into opencpn.def (NAME opencpn.exe / EXPORTS / one name per line), then:
  lib /def:opencpn.def /machine:x86 /out:opencpn.lib
  ```

## Which bitness and runtime

Build for the OpenCPN you target: the standard Windows OpenCPN is **32-bit** (`-A Win32`, target `msvc-wx32-10-x86`).
A 64-bit OpenCPN needs `-A x64`. OpenCPN ships an older `msvcp140.dll`, so `CMakeLists.txt` sets
`_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR` for MSVC. Do not remove it, or the plugin crashes on first mutex use.

## Build

From an x86 Native Tools prompt (use the x64 one for `-A x64`):

```
set WXWIN=C:\path\to\wxWidgets-3.2.x
set wxWidgets_ROOT_DIR=%WXWIN%
set wxWidgets_LIB_DIR=%WXWIN%\lib\vc14x_dll
cmake -S . -B build -G "Visual Studio 17 2022" -A Win32 -DCMAKE_SYSTEM_VERSION=10 ^
  -DOPENCPN_SOURCE_DIR=C:\path\to\OpenCPN -DOPENCPN_CONFIG_INI= ^
  -DOPENCPN_IMPLIB=C:\path\to\opencpn.lib
cmake --build build --config Release --target tarball
```

Output: `build\*.tar.gz` and `*.sha256`. Do not unpack the tarball. It contains `V-RADAR\plugins\v-radar_pi.dll`,
`V-RADAR\plugins\v-radar_pi\data\...` and `V-RADAR\metadata.xml`.

If OpenCPN reports the file as incompatible on import, compare `<target>`, `<target-version>` and `<target-arch>` in
`metadata.xml` with `CompatOS` and `CompatOsVersion` in the OpenCPN config (`opencpn.ini`).

## Test

1. Same OpenCPN version as the headers. Plugins, **Import plugin...**, choose the `.tar.gz`, restart.
2. Enable V-RADAR, Radar (radar_pi), ShipDriver, GRIB. radar_pi: radar type "Navico Halo A".
3. Allow OpenCPN through Windows Firewall (private network). The simulator uses UDP multicast; by default it stays
   on the local computer (TTL 0).
4. V-RADAR Preferences, tab **Status**: ENC cells found, 20 ships, radar found. Start ShipDriver: the radar window
   shows echoes.

## Not needed

`tools/`, `src/*_cli.cpp` (dev harnesses, built only on request) and `ci/`.
