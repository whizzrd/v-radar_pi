#pragma once
#include <functional>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "scene.h"

namespace radarsim {

// A fleet of simulated vessels around the own ship. Each sails back and forth on a straight
// leg that lies entirely in water, and is both drawn as radar echo and announced over AIS
// (position report type 1, static data type 5). Vessels that end up too far from the own
// ship are respawned in a ring around it, so the picture stays populated while the own
// ship is steered elsewhere. The fleet is reproducible for a given seed (exercises).
class AisSim {
 public:
  struct Vessel {
    int mmsi; std::string name; int ship_type; double length_m; double speed_kn;
    std::vector<std::pair<double, double>> wp;  // lat, lon
    size_t leg = 0; double lat = 0, lon = 0, cog = 0;
  };
  using WaterFn = std::function<bool(double lat, double lon)>;

  // is_water may be empty (everything is water). count vessels are kept around (ref_lat, ref_lon).
  AisSim(double ref_lat, double ref_lon, WaterFn is_water, int count = 20, unsigned seed = 1);
  void Tick(double dt_s);
  // Respawn vessels farther than max_nm from the own ship position.
  void Recenter(double lat, double lon, double max_nm = 7.0);
  std::vector<EncScene::Target> Targets() const;
  std::vector<std::string> PositionSentences() const;
  std::vector<std::string> StaticSentences() const;
  const std::vector<Vessel>& vessels() const { return vessels_; }

 private:
  bool Spawn(Vessel& v, double ref_lat, double ref_lon, double min_nm, double max_nm);
  bool PathInWater(double lat1, double lon1, double lat2, double lon2) const;

  WaterFn is_water_;
  std::mt19937 rng_;
  std::vector<Vessel> vessels_;
};

}  // namespace radarsim
