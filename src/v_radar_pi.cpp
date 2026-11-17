#include "v_radar_pi.h"

#include "grib/GribRecordSet.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <sstream>

#include <wx/filename.h>
#include <wx/log.h>
#include <wx/stdpaths.h>

extern "C" DECL_EXP opencpn_plugin* create_pi(void* ppimgr) { return new VRadarPi(ppimgr); }
extern "C" DECL_EXP void destroy_pi(opencpn_plugin* p) { delete p; }

namespace {
const double kRegionRedoNm = 4.0;  // fetch a new ENC extract when the ship moved this far from its centre
const double kPi = 3.14159265358979323846;

double DistanceNm(double lat1, double lon1, double lat2, double lon2) {
  double dy = (lat2 - lat1) * 60.0;
  double dx = (lon2 - lon1) * 60.0 * std::cos(lat1 * kPi / 180.0);
  return std::sqrt(dx * dx + dy * dy);
}
}  // namespace

// ---------------------------------------------------------------------------
// VRadarPi
// ---------------------------------------------------------------------------
VRadarPi::VRadarPi(void* ppimgr) : opencpn_plugin_120(ppimgr) {
  m_bitmap = wxBitmap(32, 32);
}
VRadarPi::~VRadarPi() = default;

int VRadarPi::Init() {
  m_config = GetOCPNConfigObject();
  LoadConfig();
  // OpenCPN's own plugin on/off switch decides whether we run at all.
  m_engine = std::make_unique<VRadarEngine>(*this);
  std::string error;
  if (!m_engine->Start(error)) {
    wxLogError("V-RADAR: cannot start simulated radar: %s", error);
    m_engine.reset();
  }
  return WANTS_CONFIG | WANTS_NMEA_EVENTS | WANTS_PREFERENCES | WANTS_PLUGIN_MESSAGING;
}

bool VRadarPi::DeInit() {
  if (m_engine) { m_engine->Stop(); m_engine.reset(); }
  SaveConfig();
  return true;
}

void VRadarPi::UpdateFix(double lat, double lon, double cog, double hdt) {
  if (!std::isfinite(lat) || !std::isfinite(lon)) return;
  std::lock_guard<std::mutex> l(m_fix_mu);
  m_has_fix = true; m_fix_lat = lat; m_fix_lon = lon;
  m_fix_heading = std::isfinite(hdt) ? hdt : (std::isfinite(cog) ? cog : m_fix_heading);
}
void VRadarPi::SetPositionFix(PlugIn_Position_Fix& p) { UpdateFix(p.Lat, p.Lon, p.Cog, NAN); }
void VRadarPi::SetPositionFixEx(PlugIn_Position_Fix_Ex& p) { UpdateFix(p.Lat, p.Lon, p.Cog, p.Hdt); }

radarsim::OwnShip VRadarPi::OwnShip() const {
  radarsim::OwnShip s;
  std::lock_guard<std::mutex> l(m_fix_mu);
  if (m_has_fix && !m_settings.use_fixed_position) {
    s.lat = m_fix_lat; s.lon = m_fix_lon; s.heading_deg = m_fix_heading;
  } else {
    s.lat = m_settings.fixed_lat; s.lon = m_settings.fixed_lon; s.heading_deg = m_settings.fixed_heading;
  }
  return s;
}

void VRadarPi::SetSettings(const VRadarSettings& s) {
  {
    std::lock_guard<std::mutex> l(m_fix_mu);  // the radar thread reads the fixed position via OwnShip()
    m_settings = s;
  }
  SaveConfig();
  if (m_engine) m_engine->ApplySettings();
}

void VRadarPi::SetPluginMessage(wxString& id, wxString& body) {
  if (m_engine) m_engine->OnPluginMessage(id, body);
}

VRadarEngine::Status VRadarPi::EngineStatus() const { return m_engine ? m_engine->GetStatus() : VRadarEngine::Status{}; }

void VRadarPi::PushSentences(const std::string& blob) const {
  std::istringstream ss(blob);
  std::string line;
  while (std::getline(ss, line))
    if (!line.empty()) PushNMEABuffer(wxString(line) + "\r\n");
}

void VRadarPi::LoadConfig() {
  if (!m_config) return;
  m_config->SetPath("/PlugIns/V-RADAR");
  auto& s = m_settings;
  m_config->Read("UseFixedPosition", &s.use_fixed_position, s.use_fixed_position);
  m_config->Read("FixedLat", &s.fixed_lat, s.fixed_lat);
  m_config->Read("FixedLon", &s.fixed_lon, s.fixed_lon);
  m_config->Read("FixedHeading", &s.fixed_heading, s.fixed_heading);
  m_config->Read("EncRadiusNm", &s.enc_radius_nm, s.enc_radius_nm);
  m_config->Read("AisEnabled", &s.ais_enabled, s.ais_enabled);
  m_config->Read("ShipCount", &s.ship_count, s.ship_count);
  m_config->Read("Seed", &s.seed, s.seed);
  m_config->Read("WeatherSource", &s.weather_source, s.weather_source);
  m_config->Read("WindKn", &s.wind_kn, s.wind_kn);
  m_config->Read("WindDir", &s.wind_dir, s.wind_dir);
  m_config->Read("WaveM", &s.wave_m, s.wave_m);
  m_config->Read("RainPreset", &s.rain_preset, s.rain_preset);
  m_config->Read("Interference", &s.interference, s.interference);
  m_config->Read("StartTransmitting", &s.start_transmitting, s.start_transmitting);
  m_config->Read("ShareNetwork", &s.share_network, s.share_network);
  m_config->Read("EncDir", &s.enc_dir, s.enc_dir);
}

void VRadarPi::SaveConfig() const {
  if (!m_config) return;
  m_config->SetPath("/PlugIns/V-RADAR");
  const auto& s = m_settings;
  m_config->Write("UseFixedPosition", s.use_fixed_position);
  m_config->Write("FixedLat", s.fixed_lat);
  m_config->Write("FixedLon", s.fixed_lon);
  m_config->Write("FixedHeading", s.fixed_heading);
  m_config->Write("EncRadiusNm", s.enc_radius_nm);
  m_config->Write("AisEnabled", s.ais_enabled);
  m_config->Write("ShipCount", s.ship_count);
  m_config->Write("Seed", s.seed);
  m_config->Write("WeatherSource", s.weather_source);
  m_config->Write("WindKn", s.wind_kn);
  m_config->Write("WindDir", s.wind_dir);
  m_config->Write("WaveM", s.wave_m);
  m_config->Write("RainPreset", s.rain_preset);
  m_config->Write("Interference", s.interference);
  m_config->Write("StartTransmitting", s.start_transmitting);
  m_config->Write("ShareNetwork", s.share_network);
  m_config->Write("EncDir", s.enc_dir);
}

// ---------------------------------------------------------------------------
// VRadarEngine
// ---------------------------------------------------------------------------
VRadarEngine::VRadarEngine(VRadarPi& plugin) : m_plugin(plugin), m_timer(this) {
  Bind(wxEVT_TIMER, &VRadarEngine::OnTimer, this);
  m_scene = std::make_shared<radarsim::SwitchableScene>();
  m_weather = std::make_shared<radarsim::Weather>();
}

VRadarEngine::~VRadarEngine() { Stop(); }

bool VRadarEngine::Start(std::string& error) {
  radarsim::SimConfig cfg;
  cfg.start_transmitting = m_plugin.settings().start_transmitting;
  cfg.share_on_network = m_plugin.settings().share_network;
  m_sim = std::make_unique<radarsim::NavicoSim>(cfg, m_scene, [this] { return m_plugin.OwnShip(); }, m_weather);
  if (!m_sim->Start(error)) { m_sim.reset(); return false; }
  m_timer.Start(1000);
  return true;
}

void VRadarEngine::Stop() {
  m_timer.Stop();
  m_cancel = true;  // the worker checks this between cells, so the join below does not wait for the whole ENC scan
  if (m_worker.joinable()) m_worker.join();
  m_cancel = false;
  FlushLog();  // on this (GUI) thread, while the DLL is certainly still loaded
  if (m_sim) { m_sim->Stop(); m_sim.reset(); }
}

void VRadarEngine::QueueLog(wxLogLevel level, std::string msg) {
  std::lock_guard<std::mutex> l(m_log_mu);
  m_log_queue.emplace_back(level, std::move(msg));
}

void VRadarEngine::FlushLog() {
  std::vector<std::pair<wxLogLevel, std::string>> pending;
  {
    std::lock_guard<std::mutex> l(m_log_mu);
    pending.swap(m_log_queue);
  }
  for (const auto& p : pending) {
    if (p.first == wxLOG_Warning) wxLogWarning("%s", p.second);
    else wxLogMessage("%s", p.second);
  }
}

void VRadarEngine::ApplySettings() {
  if (m_sim) m_sim->SetShareOnNetwork(m_plugin.settings().share_network);
  m_region_valid = false;  // re-read the ENC region on the next tick (radius or directory may have changed)
  m_rebuild_ais = true;
}

VRadarEngine::Status VRadarEngine::GetStatus() const {
  Status s;
  s.cells_found = m_cells_found; s.cells_used = m_cells_used;
  s.vessels = m_ais ? (int)m_ais->vessels().size() : 0;
  s.extracting = m_extracting; s.region_ready = m_scene && m_scene->Ready();
  if (m_sim) { s.transmitting = m_sim->transmitting(); s.range_m = m_sim->range_m(); s.nics = m_sim->network_interfaces(); s.sharing = m_sim->sharing(); }
  s.weather = m_weather_desc;
  return s;
}

void VRadarEngine::EnsureRegion(double lat, double lon) {
  if (m_extracting) return;
  if (m_region_valid && DistanceNm(m_region_lat, m_region_lon, lat, lon) < kRegionRedoNm) return;
  // ENC cells are looked up in the chart directories configured in OpenCPN (plus the optional
  // EncDir setting), so nothing depends on where a student keeps the charts.
  std::vector<std::string> dirs;
  if (!m_plugin.settings().enc_dir.IsEmpty()) dirs.push_back(m_plugin.settings().enc_dir.ToStdString());
  wxArrayString chart_dirs = GetChartDBDirArrayString();
  for (const wxString& d : chart_dirs) dirs.push_back(d.ToStdString());
  const std::string index = (wxStandardPaths::Get().GetUserDataDir() + "/v-radar/cells.idx").ToStdString();
  wxFileName::Mkdir(wxStandardPaths::Get().GetUserDataDir() + "/v-radar", wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
  const double radius = m_plugin.settings().enc_radius_nm;
  m_extracting = true;
  m_region_lat = lat; m_region_lon = lon; m_region_valid = true;
  if (m_worker.joinable()) m_worker.join();
  m_worker = std::thread([this, lat, lon, radius, dirs, index] { ExtractAndLoad(lat, lon, radius, dirs, index); });
}

void VRadarEngine::ExtractAndLoad(double lat, double lon, double radius_nm, std::vector<std::string> dirs,
                                  std::string index_file) {
  char buf[256];
  std::snprintf(buf, sizeof buf, "V-RADAR: reading ENC cells around %.3f %.3f (%zu chart directories)", lat, lon, dirs.size());
  QueueLog(wxLOG_Message, buf);
  radarsim::EncData data;
  radarsim::CollectRegion(radarsim::FindEncCells(dirs, &m_cancel), lat, lon, radius_nm, index_file, data, &m_cancel);
  if (m_cancel) { m_extracting = false; return; }  // plugin is being stopped: drop the partial result
  if (data.cells_found == 0)
    QueueLog(wxLOG_Warning, "V-RADAR: no S-57 ENC cells (*.000) found in the OpenCPN chart directories; the radar will show no land");
  else if (data.cells_used == 0) {
    std::snprintf(buf, sizeof buf, "V-RADAR: none of the %d ENC cells covers this area (%d skipped: not downloaded or unreadable)",
                  data.cells_found, data.cells_skipped);
    QueueLog(wxLOG_Warning, buf);
  } else if (data.cells_skipped > 0) {
    std::snprintf(buf, sizeof buf, "V-RADAR: %d ENC cells skipped (cloud placeholders or unreadable)", data.cells_skipped);
    QueueLog(wxLOG_Message, buf);
  }
  auto enc = std::make_shared<radarsim::EncScene>();
  enc->SetData(data);
  m_scene->Set(enc);
  m_cells_found = data.cells_found; m_cells_used = data.cells_used;
  std::snprintf(buf, sizeof buf, "V-RADAR: ENC region loaded: %d cells, %zu edges, %zu marks", data.cells_used, data.segs.size(), data.pts.size());
  QueueLog(wxLOG_Message, buf);
  m_extracting = false;
}

// ---- weather: manual presets or GRIB from grib_pi -------------------------------------------------

namespace {
// Tiny JSON field readers: the grib_pi messages are flat objects with number or string values.
bool JsonNumber(const wxString& body, const char* key, double& out) {
  size_t k = body.find(wxString("\"") + key + "\"");
  if (k == wxString::npos) return false;
  size_t c = body.find(':', k);
  if (c == wxString::npos) return false;
  return body.Mid(c + 1).Trim(false).BeforeFirst(',').BeforeFirst('}').ToDouble(&out);
}
bool JsonString(const wxString& body, const char* key, wxString& out) {
  size_t k = body.find(wxString("\"") + key + "\"");
  if (k == wxString::npos) return false;
  size_t c = body.find(':', k);
  size_t a = body.find('"', c);
  if (a == wxString::npos) return false;
  size_t b = body.find('"', a + 1);
  if (b == wxString::npos) return false;
  out = body.Mid(a + 1, b - a - 1);
  return true;
}
const int kGribMin = 4001, kGribMax = 5000;  // grib_pi versions whose GribRecordSet layout we mirror (4.1 .. 5.0)
}  // namespace

void VRadarEngine::OnPluginMessage(const wxString& id, const wxString& body) {
  if (id == "GRIB_TIMELINE") {
    m_grib_timeline = body;  // user moved the grib_pi time slider: use that forecast time
    m_grib_last_request = -1000;
    return;
  }
  if (id != "GRIB_TIMELINE_RECORD") return;
  if (m_plugin.settings().weather_source != 1) return;
  double maj = 0, min = 0;
  if (JsonNumber(body, "GribVersionMajor", maj) && JsonNumber(body, "GribVersionMinor", min)) {
    int v = (int)(1000 * maj + min);
    if (v < kGribMin || v > kGribMax) {
      m_grib_valid = false;
      m_grib_note = "grib_pi version not supported (need 4.1 - 5.0)";
      return;
    }
  }
  wxString sptr;
  if (!JsonString(body, "TimelineSetPtr", sptr)) { m_grib_valid = false; m_grib_note = "no data for this time in grib_pi"; return; }
  void* ptr = nullptr;
  wxCharBuffer buf = sptr.To8BitData();
  if (std::sscanf(buf.data(), "%p", &ptr) != 1 || !ptr) { m_grib_valid = false; m_grib_note = "no data for this time in grib_pi"; return; }
  SampleGrib(ptr, m_plugin.OwnShip());
}

void VRadarEngine::SampleGrib(void* record_set, const radarsim::OwnShip& ship) {
  GribRecordSet* set = (GribRecordSet*)record_set;
  radarsim::WeatherState w;
  double speed = 0, dir = 0;
  const GribRecord* vx = set->m_GribRecordPtrArray[Idx_WIND_VX];
  const GribRecord* vy = set->m_GribRecordPtrArray[Idx_WIND_VY];
  if (vx && vy && GribRecord::getInterpolatedValues(speed, dir, vx, vy, ship.lon, ship.lat)) {
    w.wind_ms = speed; w.wind_from_deg = std::fmod(dir + 360.0, 360.0);
  }
  auto val = [&](int idx, double def) {
    const GribRecord* r = set->m_GribRecordPtrArray[idx];
    if (!r) return def;
    double v = r->getInterpolatedValue(ship.lon, ship.lat);
    return v == GRIB_NOTDEF ? def : v;
  };
  double gust = val(Idx_WIND_GUST, 0);
  w.gust_ms = gust > 0 ? gust : 0;
  double hs = val(Idx_HTSIGW, -1);
  w.wave_m = hs >= 0 ? hs : 0.012 * w.wind_ms * w.wind_ms;  // no wave data: rough fetch-limited estimate
  double cape = val(Idx_CAPE, 0);
  double gustiness = w.gust_ms > 0 ? (w.gust_ms - w.wind_ms) / 10.0 : 0;
  w.convective = std::min(1.0, std::max(cape / 1500.0, gustiness));

  // rain: sample a 60 x 60 NM field around the ship (2 NM steps) from the precipitation or reflectivity record
  radarsim::WeatherState::Grid& g = w.grid;
  g.nx = g.ny = 31; g.dlat = 2.0 / 60.0; g.lat0 = ship.lat - 15 * g.dlat;
  g.dlon = g.dlat / std::max(0.1, std::cos(ship.lat * kPi / 180.0)); g.lon0 = ship.lon - 15 * g.dlon;
  g.rate.assign((size_t)g.nx * g.ny, 0.f);
  const GribRecord* precip = set->m_GribRecordPtrArray[Idx_PRECIP_TOT];
  const GribRecord* refl = set->m_GribRecordPtrArray[Idx_COMP_REFL];
  double max_rate = 0;
  for (int j = 0; j < g.ny; j++)
    for (int i = 0; i < g.nx; i++) {
      double lat = g.lat0 + j * g.dlat, lon = g.lon0 + i * g.dlon, rate = 0;
      if (precip) { double v = precip->getInterpolatedValue(lon, lat); if (v != GRIB_NOTDEF && v > 0) rate = v / 3.0; }  // mm per 3 h
      if (refl) {
        double dbz = refl->getInterpolatedValue(lon, lat);
        if (dbz != GRIB_NOTDEF && dbz > 5) rate = std::max(rate, std::pow(std::pow(10.0, dbz / 10.0) / 200.0, 1.0 / 1.6));
      }
      g.rate[(size_t)j * g.nx + i] = (float)rate;
      max_rate = std::max(max_rate, rate);
    }
  m_grib_state = std::move(w);
  m_grib_valid = true;
  char b[160];
  std::snprintf(b, sizeof b, "GRIB: wind %.0f kn from %.0f, waves %.1f m, rain up to %.1f mm/h", m_grib_state.wind_ms * 1.944,
                m_grib_state.wind_from_deg, m_grib_state.wave_m, max_rate);
  m_grib_note = b;
}

void VRadarEngine::UpdateWeather(const radarsim::OwnShip& ship) {
  const VRadarSettings& s = m_plugin.settings();
  auto w = std::make_shared<radarsim::WeatherState>();
  if (s.weather_source == 1) {
    // ask grib_pi for the record set of the selected forecast time (now, unless its time slider was used)
    if (m_tick - m_grib_last_request >= 30) {
      m_grib_last_request = m_tick;
      wxString req;
      wxString tl = m_grib_timeline;
      if (tl.IsEmpty()) {
        wxDateTime t = wxDateTime::Now().ToUTC();
        req.Printf("{\"Day\":%d,\"Month\":%d,\"Year\":%d,\"Hour\":%d,\"Minute\":%d,\"Second\":%d}", t.GetDay(),
                   (int)t.GetMonth(), t.GetYear(), t.GetHour(), t.GetMinute(), t.GetSecond());
      } else {
        req = tl;  // same fields as the timeline message
      }
      SendPluginMessage("GRIB_TIMELINE_RECORD_REQUEST", req);
    }
    if (m_grib_valid) {
      *w = m_grib_state;
      m_weather_desc = m_grib_note;
    } else {
      w->wind_ms = s.wind_kn * 0.5144; w->wind_from_deg = s.wind_dir; w->wave_m = s.wave_m;
      m_weather_desc = (m_grib_note.empty() ? "GRIB: waiting for grib_pi (enable it and load a GRIB file)" : "GRIB: " + m_grib_note) +
                       "; using the manual weather meanwhile";
    }
  } else {
    w->wind_ms = s.wind_kn * 0.5144; w->wind_from_deg = s.wind_dir; w->wave_m = s.wave_m;
    w->convective = s.rain_preset >= 3 ? 0.9 : (s.rain_preset == 2 ? 0.6 : (s.rain_preset == 1 ? 0.2 : 0.0));
    if (m_cells_preset != s.rain_preset || m_cells_seed != (unsigned)s.seed) {
      m_cells = radarsim::MakeRainPreset(s.rain_preset, ship.lat, ship.lon, (unsigned)s.seed);
      m_cells_preset = s.rain_preset; m_cells_seed = (unsigned)s.seed;
    }
    // The pattern drifts with the wind: cells that left the area are re-entered upwind, so there is always
    // weather to see however long the exercise runs and wherever the ship goes.
    {
      const double mx = 111320.0 * std::cos(ship.lat * kPi / 180.0), my = 110574.0;
      const double from = s.wind_dir * kPi / 180.0;
      std::uniform_real_distribution<double> u(-1.0, 1.0);
      for (auto& c : m_cells) {
        double e = c.lon * mx + m_drift_e - ship.lon * mx, n = c.lat * my + m_drift_n - ship.lat * my;  // world position
        if (std::sqrt(e * e + n * n) > 20000.0) {
          double dist = 17000.0, cross = u(m_rng) * 12000.0;
          double we = std::sin(from) * dist + std::cos(from) * cross, wn = std::cos(from) * dist - std::sin(from) * cross;
          c.lon = ship.lon + (we - m_drift_e) / mx;
          c.lat = ship.lat + (wn - m_drift_n) / my;
        }
      }
    }
    w->cells = m_cells;
    static const char* const names[] = {"no rain", "light showers", "heavy showers", "squall line"};
    char b[160];
    std::snprintf(b, sizeof b, "Manual: wind %d kn from %d, waves %.1f m, %s", s.wind_kn, s.wind_dir, s.wave_m,
                  names[std::max(0, std::min(3, s.rain_preset))]);
    m_weather_desc = b;
  }
  // the rain pattern drifts downwind at roughly the wind speed
  const double to = (w->wind_from_deg + 180.0) * kPi / 180.0;
  m_drift_e += std::sin(to) * w->wind_ms * 0.8; m_drift_n += std::cos(to) * w->wind_ms * 0.8;  // timer = 1 s
  w->interference = std::max(0, std::min(2, s.interference));
  if (s.weather_source == 1 && m_grib_valid) { w->drift_e = 0; w->drift_n = 0; }  // a GRIB field is re-read, not advected
  else { w->drift_e = m_drift_e; w->drift_n = m_drift_n; }
  m_weather->Set(w);
}

void VRadarEngine::OnTimer(wxTimerEvent&) {
  m_tick++;
  FlushLog();
  radarsim::OwnShip ship = m_plugin.OwnShip();
  EnsureRegion(ship.lat, ship.lon);
  UpdateWeather(ship);
  const VRadarSettings& cfg = m_plugin.settings();
  if (m_rebuild_ais.exchange(false)) m_ais.reset();
  if (!cfg.ais_enabled) {
    if (m_ais) { m_ais.reset(); m_scene->SetTargets({}); }
    return;
  }
  if (!m_ais) {
    if (!m_scene->Ready()) return;  // fleet is placed on water, so wait for the ENC extract
    auto scene = m_scene;
    m_ais = std::make_unique<radarsim::AisSim>(
        ship.lat, ship.lon, [scene](double la, double lo) { return scene->IsWater(la, lo); },
        std::max(0, std::min(cfg.ship_count, 60)), (unsigned)cfg.seed);
    wxLogMessage("V-RADAR: %d simulated vessels", (int)m_ais->vessels().size());
  }
  m_ais->Recenter(ship.lat, ship.lon);
  m_ais->Tick(1.0);
  m_scene->SetTargets(m_ais->Targets());
  for (auto& s : m_ais->PositionSentences()) m_plugin.PushSentences(s);
  if (m_tick % 10 == 0)
    for (auto& s : m_ais->StaticSentences()) m_plugin.PushSentences(s);
}
