set(PKG_NAME v-radar_pi)
set(PKG_VERSION 0.1.0)
set(PKG_PRERELEASE "")

set(DISPLAY_NAME V-RADAR)
set(PLUGIN_API_NAME V-RADAR)
set(PKG_SUMMARY "Simulated Navico HALO radar fed from ENC charts and AIS")
set(PKG_DESCRIPTION [=[
Simulates a Navico (B&G / Simrad / Lowrance) HALO radar on the local network.
Radar echoes are computed from the own ship position by ray-casting against
S-57 ENC land areas and navigation marks, plus AIS targets. The radar is seen
by the Radar plugin (radar_pi) in this or any other OpenCPN, or by plotters
that speak the Navico protocol. For demonstration and testing without hardware.
]=])

set(PKG_AUTHOR "Joris van Lier")
set(PKG_IS_OPEN_SOURCE "yes")
set(PKG_HOMEPAGE https://github.com/whizzrd/v-radar_pi)
set(PKG_INFO_URL https://github.com/whizzrd/v-radar_pi)

set(OCPN_TEST_REPO "whizzrd/v-radar-alpha" CACHE STRING "Default repository for untagged builds")
set(OCPN_BETA_REPO "whizzrd/v-radar-beta" CACHE STRING "Default repository for beta builds")
set(OCPN_RELEASE_REPO "whizzrd/v-radar-prod" CACHE STRING "Default repository for release builds")
