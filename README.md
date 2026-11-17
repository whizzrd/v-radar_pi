# V-RADAR

An OpenCPN plugin that simulates a **Navico HALO radar** on the local network, for training and demonstrations
without radar hardware. The Radar plugin (`radar_pi`) connects to it like to a real radar; the picture is computed
from ENC charts, simulated ships and weather.

Made for maritime students who practise radar observation at home with OpenCPN.

## What it simulates

- **Coast and structures** from S-57 ENC cells: land, built-up areas, quays, buoys, beacons and other reflectors.
  Cells are found through OpenCPN's own chart directories (unencrypted S-57 `.000` only).
- **20 other ships** (cargo, tanker, ferry, trawler, sailing, tug, pilot, ...), also sent as AIS targets. The own ship is
  steered with the ShipDriver plugin.
- **Weather and clutter**: sea clutter, rain and squalls, receiver noise, interference. Weather is set by hand or taken
  from a GRIB file loaded in the GRIB plugin.
- **Radar controls** as on a HALO, working in radar_pi: gain, sea, rain, noise rejection, target expansion and
  separation, interference rejection, target boost, fast scan, and the mode presets. Students remove clutter with the
  same controls as on a real set. Strengths are estimates, not calibrated against real hardware.

Not simulated: Doppler and side lobe suppression (accepted, no effect), ARPA beyond what radar_pi provides.

## Install

Download the file for your system and import it in OpenCPN: see **[INSTALL.md](INSTALL.md)**.
Available for Windows (x86) and macOS Intel; Linux and macOS Apple Silicon need their own build.

## Build

See **[BUILD.md](BUILD.md)** (macOS, Linux and Windows).

## Settings

OpenCPN, Options, Plugins, V-RADAR, Preferences. Tabs: Status, Own ship, Ships, Charts, Weather, Radar.
Switching the simulator on or off is OpenCPN's own plugin switch.

## Network

The radar data is UDP multicast in the Navico format. By default it stays on your own computer (TTL 0). Enable
"Share on network" only to let another computer see the radar. Sharing generates about 4 Mbit/s and some routers and
switches flood multicast to all ports, so leave it off unless needed.

## Layout

| Path | Contents |
|---|---|
| `src/` | plugin and simulator (`navico_sim`, `scene`/`s57_reader`, `ais_sim`, `weather`, `clutter`, `prefs_dialog`) |
| `src/grib/` | GRIB record reader copied from grib_pi (GPL), see its README |
| `data/` | plugin data files |
| `tools/` | development helpers for validating the simulation |
| `cmake/`, `Plugin.cmake` | OpenCPN plugin packaging |

## Status

Version 0.1. Not verified in OpenCPN: GRIB weather input (units and wind direction are assumptions), tarball import on
every platform. Not handled: ENC update files (`.001`), encrypted charts.
