#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <random>
#include <thread>
#include <vector>

#include <wx/fileconf.h>
#include <wx/timer.h>
#include <wx/wx.h>

#include "ais_sim.h"
#include "config.h"
#include "navico_sim.h"
#include "ocpn_plugin.h"
#include "scene.h"
#include "weather.h"

struct VRadarSettings {
  bool use_fixed_position = false;   // ignore GPS, use the fixed position below (demo mode)
  double fixed_lat = 52.6396;
  double fixed_lon = 5.0670;
  double fixed_heading = 0.0;
  double enc_radius_nm = 12.0;       // radius of the ENC extract around the ship
  bool ais_enabled = true;           // simulated AIS vessels (also drawn as radar echoes)
  int ship_count = 20;               // number of simulated vessels around the own ship
  int seed = 1;                      // same seed = same fleet (repeatable exercises)
  int weather_source = 0;            // 0 manual, 1 GRIB (grib_pi)
  int wind_kn = 15;                  // manual weather: wind speed (knots)
  int wind_dir = 225;                // manual weather: wind direction (from, degrees)
  double wave_m = 0.8;               // manual weather: significant wave height (m)
  int rain_preset = 1;               // manual weather: 0 none, 1 light showers, 2 heavy showers, 3 squall line
  int interference = 1;              // other radars nearby: 0 none, 1 some, 2 many
  bool start_transmitting = true;    // demo convenience: do not wait for a transmit command
  bool share_network = false;        // let other computers on the network see the radar (heavy multicast stream)
  wxString enc_dir;                  // extra ENC directory; the OpenCPN chart directories are always searched
};

class VRadarPi;

// Runs on the GUI thread: follows the ship, refreshes the ENC region, drives the AIS vessels.
class VRadarEngine : public wxEvtHandler {
 public:
  explicit VRadarEngine(VRadarPi& plugin);
  ~VRadarEngine() override;
  bool Start(std::string& error);
  void Stop();
  // Called after the settings changed: re-reads the ENC region and rebuilds the fleet.
  void ApplySettings();
  // Messages from other plugins (grib_pi): timeline time and the GRIB record set for that time.
  void OnPluginMessage(const wxString& id, const wxString& body);
  struct Status {
    int cells_found = 0, cells_used = 0, vessels = 0;
    bool extracting = false, region_ready = false, transmitting = false;
    int range_m = 0;
    int nics = 0;  // network interfaces in use (0 = no network)
    bool sharing = false;
    std::string weather;  // one-line description of the weather in use
  };
  Status GetStatus() const;

 private:
  void OnTimer(wxTimerEvent&);
  void EnsureRegion(double lat, double lon);
  void UpdateWeather(const radarsim::OwnShip& ship);
  void SampleGrib(void* record_set, const radarsim::OwnShip& ship);
  void ExtractAndLoad(double lat, double lon, double radius_nm, std::vector<std::string> dirs, std::string index_file);

  VRadarPi& m_plugin;
  wxTimer m_timer;
  std::shared_ptr<radarsim::SwitchableScene> m_scene;
  std::unique_ptr<radarsim::NavicoSim> m_sim;
  std::unique_ptr<radarsim::AisSim> m_ais;
  double m_ais_lat = 0, m_ais_lon = 0;
  bool m_region_valid = false;
  double m_region_lat = 0, m_region_lon = 0;
  std::atomic<bool> m_extracting{false};
  std::atomic<int> m_cells_found{0}, m_cells_used{0};
  std::atomic<bool> m_rebuild_ais{false};
  std::thread m_worker;
  long m_tick = 0;

  // weather
  std::shared_ptr<radarsim::Weather> m_weather;
  std::vector<radarsim::WeatherState::Cell> m_cells;
  int m_cells_preset = -1; unsigned m_cells_seed = 0; double m_cells_lat = 0, m_cells_lon = 0;
  double m_drift_e = 0, m_drift_n = 0;
  std::mt19937 m_rng{12345};
  bool m_grib_valid = false;
  radarsim::WeatherState m_grib_state;
  std::string m_grib_note;
  wxString m_grib_timeline;  // last GRIB_TIMELINE message body (selects the forecast time)
  long m_grib_last_request = -1000;
  std::string m_weather_desc;
};

class VRadarPi : public opencpn_plugin_120 {
 public:
  explicit VRadarPi(void* ppimgr);
  ~VRadarPi() override;

  int Init() override;
  bool DeInit() override;
  int GetAPIVersionMajor() override { return 1; }
  int GetAPIVersionMinor() override { return 20; }
  int GetPlugInVersionMajor() override { return PLUGIN_VERSION_MAJOR; }
  int GetPlugInVersionMinor() override { return PLUGIN_VERSION_MINOR; }
  int GetPlugInVersionPatch() override { return PLUGIN_VERSION_PATCH; }
  int GetPlugInVersionPost() override { return PLUGIN_VERSION_TWEAK; }
  const char* GetPlugInVersionPre() override { return PKG_PRERELEASE; }
  const char* GetPlugInVersionBuild() override { return PKG_BUILD_INFO; }
  wxBitmap* GetPlugInBitmap() override { return &m_bitmap; }
  wxString GetCommonName() override { return PLUGIN_API_NAME; }
  wxString GetShortDescription() override { return PKG_SUMMARY; }
  wxString GetLongDescription() override { return PKG_DESCRIPTION; }
  void SetPositionFix(PlugIn_Position_Fix& pfix) override;
  void SetPositionFixEx(PlugIn_Position_Fix_Ex& pfix) override;

  void ShowPreferencesDialog(wxWindow* parent) override;
  void SetPluginMessage(wxString& message_id, wxString& message_body) override;
  const VRadarSettings& settings() const { return m_settings; }
  // Replaces the settings (from the preferences dialog), saves them and applies them to the running simulator.
  void SetSettings(const VRadarSettings& s);
  VRadarEngine::Status EngineStatus() const;
  // Own ship as the radar sees it: GPS fix when available (unless demo mode), else fixed position.
  radarsim::OwnShip OwnShip() const;
  void PushSentences(const std::string& blob) const;

 private:
  void LoadConfig();
  void SaveConfig() const;
  void UpdateFix(double lat, double lon, double cog, double hdt);

  VRadarSettings m_settings;
  wxBitmap m_bitmap;
  wxFileConfig* m_config = nullptr;
  std::unique_ptr<VRadarEngine> m_engine;
  mutable std::mutex m_fix_mu;
  bool m_has_fix = false;
  double m_fix_lat = 0, m_fix_lon = 0, m_fix_heading = 0;
};
