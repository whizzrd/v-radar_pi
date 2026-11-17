#pragma once
#include <cstdint>

#include "scene.h"
#include "weather.h"

namespace radarsim {

// What the operator has set on the radar (percent 0..100 as shown by the Radar plugin).
struct RadarControls {
  int gain_pct = 50;
  bool gain_auto = false;
  int sea_pct = 0;
  int sea_mode = 0;      // 0 manual, 1 harbour (auto), 2 offshore (auto)
  int rain_pct = 0;
  int noise_rejection = 0;          // 0..2
  int target_expansion = 0;         // 0..2
  int target_boost = 0;             // 0..2
  int target_separation = 0;        // 0..3
  int interference_rejection = 0;   // 0..3
  int local_interference_rejection = 0;  // 0..3
};

// Receiver model: turns the clean echo picture of the scene into what a real radar shows, with sea
// clutter (wind and waves, strongest close in and upwind), rain/squall echoes, receiver noise and the
// effect of the GAIN, SEA (STC) and RAIN (FTC) controls. Echo strength is on the 0..15 scale.
class Clutter {
 public:
  // out[0..n): the scene's echo for one spoke; modified in place. scan = spoke counter (varies the speckle).
  // land[i] != 0 marks samples on land: there is no sea clutter there (may be null).
  void Apply(const OwnShip& ship, double bearing_deg, double range_m, uint32_t scan, const WeatherState& w,
             const RadarControls& c, uint8_t* out, int n, const uint8_t* land = nullptr);
};

}  // namespace radarsim
