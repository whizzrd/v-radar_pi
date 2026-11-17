# Installing V-RADAR

V-RADAR is installed by hand from a `.tar.gz`; it is not in the OpenCPN plugin catalog. It works together with
three other plugins and needs ENC charts.

## 1. What you need

- **OpenCPN** 5.12 or newer (the plugin was tested with 5.14).
- These plugins, installed and enabled in OpenCPN (Options, **Plugins**):
  - **Radar** (`radar_pi`): the radar display. Select it in the plugin list and click **Install** (internet needed).
  - **ShipDriver** (`shipdriver_pi`): steers your own ship. Install it the same way.
  - **GRIB** (`grib_pi`): ships with OpenCPN, only needs to be enabled. Used for wind, waves and rain.
- **ENC charts** (S-57, unencrypted `.000` cells) of the area you want to practise in, see step 4.

## 2. Download V-RADAR

Download the file for your system, `V-RADAR-<version>_<os>-<arch>.tar.gz` (do not unpack it).

| System | File name contains | Notes |
|---|---|---|
| **Windows** (OpenCPN 32-bit, the standard Windows build) | `msvc-wx32-10-x86` | Built, not yet tested on a clean OpenCPN install. |
| **macOS Intel** | `darwin-wx32-15.7.3-x86_64` | Tested. |

Linux and macOS Apple Silicon (arm64) are not available yet. OpenCPN refuses a file that does not match your
system, so pick the one in the table.

## 3. Install it

OpenCPN: Options, **Plugins**, **Import plugin...**, choose the `.tar.gz`, restart OpenCPN if asked. Then enable
**V-RADAR** in the plugin list (together with Radar, ShipDriver and GRIB).

## 4. Get the ENC charts

Inland ENC (iENC) for European waterways is available in OpenCPN itself through the **Chart Downloader**
(a built-in plugin) with the catalog **"EURIS - All European waterways Inland ENC Charts"**:

1. Options, **Charts**, tab **Chart Downloader**. Enable the Chart Downloader plugin first if the tab is missing.
2. Select the catalog *EURIS - All European waterways Inland ENC Charts*, click **Update** and then download the
   cells for your area (or all of them). They are stored in the `ENC/EURIS` folder of the OpenCPN data folder
   (Windows `C:\ProgramData\opencpn`, macOS `~/Library/Preferences/opencpn`).
3. Options, **Charts**, tab **Chart Files**: check that the folder `ENC/EURIS` is in the list. If not, click
   **Add Directory** and add it, then let OpenCPN scan the charts.

The same data can be downloaded manually from the EURIS portal
(https://www.eurisportal.eu/pages/default.aspx?path=Diensten%2FVaarkaarten%2FDownloadOverview): unpack the files
to a folder and add that folder under Options, Charts, Chart Files.

V-RADAR finds the cells through the chart folders of OpenCPN. It reads only unencrypted S-57 cells (`.000`);
encrypted charts (for example oeSENC / o-charts) and update files (`.001` and up) are not used.

## 5. Set up and sail

1. Radar plugin: choose radar type **Navico Halo A**. Settings of the simulator: Plugins, V-RADAR, Preferences.
2. ShipDriver: set course and speed, **Start**. The radar window appears when position and heading are available.
3. Set the weather in V-RADAR Preferences (manual, or from a GRIB file opened in the GRIB plugin) and use the
   GAIN, SEA and RAIN controls of the radar to remove clutter.

## Remove or update

Install a newer `.tar.gz` the same way (the old version is replaced), or use the plugin list.

On Windows, allow OpenCPN through the firewall on private networks when asked: the simulated radar sends its
data over the local network (by default it stays on your own computer).
