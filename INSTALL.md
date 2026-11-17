# Installing V-RADAR (manual, no catalog)

1. Download the file for your system, `V-RADAR-<version>_<os>-<arch>.tar.gz` (do not unpack it).
   Currently available: **macOS Intel (x86_64)**. Windows, Linux and macOS Apple Silicon need their own build.
2. OpenCPN: Options, **Plugins**, **Import plugin...**, choose the `.tar.gz`. OpenCPN checks that it fits your
   system and installs it.
3. Enable **V-RADAR** in the plugin list, and also **Radar** (radar_pi), **ShipDriver** and **GRIB**.
4. Set your ENC chart folder in Options, Charts (S-57 `.000` cells, unencrypted). V-RADAR finds them there.
5. Radar plugin: choose radar type "Navico Halo A". Settings of the simulator: Plugins, V-RADAR, Preferences.
6. Drive: ShipDriver, set course and speed, Start. The radar window appears when position and heading are available.

Remove or update: install a newer `.tar.gz` the same way (the old version is replaced), or use the plugin list.

## Building the file yourself

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target tarball
```
The result (`.tar.gz`, `.sha256` and the metadata `.xml`) is written to `build/`.
