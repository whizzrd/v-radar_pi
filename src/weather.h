#pragma once
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace radarsim {

// The weather that drives radar clutter: sea state from wind and waves, rain and squalls from a
// precipitation field. It comes either from manual settings or from a GRIB file loaded in grib_pi.
struct WeatherState {
  double wind_ms = 7.7;        // mean wind speed
  double wind_from_deg = 225;  // direction the wind blows from
  double gust_ms = 0;          // gust speed (0 = unknown)
  double wave_m = 0.8;         // significant wave height
  double convective = 0;       // 0..1: squally/showery air (from CAPE, gusts or manual preset)
  int interference = 0;        // other radars nearby: 0 none, 1 some, 2 many (radial dotted streaks)

  // Precipitation on a lat/lon grid (mm/h), e.g. sampled from GRIB. Empty = no gridded rain.
  struct Grid {
    double lat0 = 0, lon0 = 0, dlat = 0, dlon = 0;
    int nx = 0, ny = 0;
    std::vector<float> rate;
    bool empty() const { return nx < 2 || ny < 2; }
  } grid;

  // Hand-placed rain cells (manual presets), metres east/north of a reference point.
  struct Cell { double lat, lon, radius_m, rate_mmh; };
  std::vector<Cell> cells;

  // The whole rain pattern drifts with the wind; offset in metres east/north.
  double drift_e = 0, drift_n = 0;
};

// Rain rate in mm/h at a position, with the fine structure (showers, gaps) that GRIB cannot give.
double RainRate(const WeatherState& w, double lat, double lon);

// Thread-safe holder: the engine replaces the state, the radar thread reads it.
class Weather {
 public:
  Weather() : state_(std::make_shared<WeatherState>()) {}
  std::shared_ptr<const WeatherState> Get() const {
    std::lock_guard<std::mutex> l(mu_);
    return state_;
  }
  void Set(std::shared_ptr<const WeatherState> s) {
    std::lock_guard<std::mutex> l(mu_);
    state_ = std::move(s);
  }
 private:
  mutable std::mutex mu_;
  std::shared_ptr<const WeatherState> state_;
};

// Manual presets for rain: 0 none, 1 light showers, 2 heavy showers, 3 squall line.
// Cells are placed deterministically around (lat, lon) from the seed.
std::vector<WeatherState::Cell> MakeRainPreset(int preset, double lat, double lon, unsigned seed);

}  // namespace radarsim
