#include "ais_sim.h"

#include <algorithm>
#include <cmath>

#include "ais_encoder.h"

namespace radarsim {
namespace {
const double kPi = 3.14159265358979323846;
const double kKnotsToMs = 0.514444;

double Bearing(double lat1, double lon1, double lat2, double lon2) {
  double dy = (lat2 - lat1) * 110574.0;
  double dx = (lon2 - lon1) * 111320.0 * std::cos(lat1 * kPi / 180.0);
  double b = std::atan2(dx, dy) * 180.0 / kPi;
  return b < 0 ? b + 360.0 : b;
}
double Dist(double lat1, double lon1, double lat2, double lon2) {
  double dy = (lat2 - lat1) * 110574.0;
  double dx = (lon2 - lon1) * 111320.0 * std::cos(lat1 * kPi / 180.0);
  return std::sqrt(dx * dx + dy * dy);
}
}  // namespace

namespace {
struct Kind { const char* prefix; int type; double len_lo, len_hi, kn_lo, kn_hi; };
// Mix roughly like IJsselmeer traffic: cargo, tanker, passenger, fishing, sailing, pleasure, tug, pilot.
const Kind kKinds[] = {
    {"CARGO",   70, 85, 135, 8, 12}, {"TANKER",  80, 90, 125, 7, 11}, {"FERRY",   60, 40,  70, 9, 14},
    {"TRAWLER", 30, 15,  30, 4,  8}, {"TJALK",   36, 12,  20, 4,  7}, {"YACHT",   37,  9,  16, 4,  8},
    {"MOTOR",   37,  8,  14, 8, 18}, {"TUG",     52, 20,  35, 5,  9}, {"PILOT",   50, 12,  20, 10, 20},
};
const char* const kNames[] = {"ANNA", "DELTA", "NOVA", "ORION", "LUNA", "ATLAS", "WAVE", "ZEPHYR", "MARIE",
                               "FRISIA", "NEPTUNE", "ALBATROS", "VERA", "OSCAR", "SIRIUS", "THEA", "JOLLY",
                               "KOOPMAN", "LISA", "MERCUR", "BOREAS", "TRITON", "ROMY", "HERMES"};
}  // namespace

bool AisSim::PathInWater(double lat1, double lon1, double lat2, double lon2) const {
  if (!is_water_) return true;
  double len = Dist(lat1, lon1, lat2, lon2);
  int n = std::max(2, (int)(len / 100.0));
  for (int i = 0; i <= n; i++) {
    double f = (double)i / n;
    if (!is_water_(lat1 + (lat2 - lat1) * f, lon1 + (lon2 - lon1) * f)) return false;
  }
  return true;
}

bool AisSim::Spawn(Vessel& v, double ref_lat, double ref_lon, double min_nm, double max_nm) {
  std::uniform_real_distribution<double> u01(0.0, 1.0);
  for (int attempt = 0; attempt < 120; attempt++) {
    double r = (min_nm + (max_nm - min_nm) * u01(rng_)) * 1852.0;
    double a = u01(rng_) * 2 * kPi;
    double lat = ref_lat + r * std::cos(a) / 110574.0;
    double lon = ref_lon + r * std::sin(a) / (111320.0 * std::cos(ref_lat * kPi / 180.0));
    double hdg = u01(rng_) * 2 * kPi;
    double leg = 2000.0 + 4000.0 * u01(rng_);
    double lat2 = lat + leg * std::cos(hdg) / 110574.0;
    double lon2 = lon + leg * std::sin(hdg) / (111320.0 * std::cos(lat * kPi / 180.0));
    if (!PathInWater(lat, lon, lat2, lon2)) continue;
    v.wp = {{lat, lon}, {lat2, lon2}};
    v.lat = lat; v.lon = lon; v.leg = 1;
    v.cog = Bearing(lat, lon, lat2, lon2);
    return true;
  }
  return false;
}

AisSim::AisSim(double ref_lat, double ref_lon, WaterFn is_water, int count, unsigned seed)
    : is_water_(std::move(is_water)), rng_(seed) {
  std::uniform_real_distribution<double> u01(0.0, 1.0);
  const int nkinds = (int)(sizeof kKinds / sizeof kKinds[0]);
  const int nnames = (int)(sizeof kNames / sizeof kNames[0]);
  for (int i = 0; i < count; i++) {
    const Kind& k = kKinds[i % nkinds];
    Vessel v;
    v.mmsi = 244010001 + i;
    v.name = std::string(k.prefix) + " " + kNames[(i * 7 + 3) % nnames];
    v.ship_type = k.type;
    v.length_m = k.len_lo + (k.len_hi - k.len_lo) * u01(rng_);
    v.speed_kn = k.kn_lo + (k.kn_hi - k.kn_lo) * u01(rng_);
    if (Spawn(v, ref_lat, ref_lon, 0.4, 6.0)) vessels_.push_back(std::move(v));
  }
}

void AisSim::Recenter(double lat, double lon, double max_nm) {
  for (Vessel& v : vessels_) {
    if (Dist(v.lat, v.lon, lat, lon) / 1852.0 <= max_nm) continue;
    Vessel w = v;
    if (Spawn(w, lat, lon, max_nm - 2.0, max_nm - 0.5)) v = std::move(w);
  }
}

void AisSim::Tick(double dt_s) {
  for (Vessel& v : vessels_) {
    auto& target = v.wp[v.leg];
    double d = Dist(v.lat, v.lon, target.first, target.second);
    double move = v.speed_kn * kKnotsToMs * dt_s;
    if (move >= d) {
      v.lat = target.first; v.lon = target.second;
      v.leg = (v.leg + 1) % v.wp.size();
    } else {
      double b = Bearing(v.lat, v.lon, target.first, target.second) * kPi / 180.0;
      v.lat += move * std::cos(b) / 110574.0;
      v.lon += move * std::sin(b) / (111320.0 * std::cos(v.lat * kPi / 180.0));
    }
    auto& nxt = v.wp[v.leg];
    v.cog = Bearing(v.lat, v.lon, nxt.first, nxt.second);
  }
}

std::vector<EncScene::Target> AisSim::Targets() const {
  std::vector<EncScene::Target> t;
  for (const Vessel& v : vessels_)
    t.push_back({v.lat, v.lon, v.length_m / 2.0, (uint8_t)(v.length_m > 40 ? 14 : 12)});
  return t;
}

static VirtualVesselReport ToReport(const AisSim::Vessel& v) {
  VirtualVesselReport r;
  r.mmsi = v.mmsi; r.latitude = v.lat; r.longitude = v.lon; r.name = v.name; r.ship_type = v.ship_type;
  r.sog_knots = v.speed_kn; r.cog_degrees = v.cog; r.true_heading = (int)v.cog;
  r.dimension_to_bow = (int)(v.length_m * 0.7); r.dimension_to_stern = (int)(v.length_m * 0.3);
  r.dimension_to_port = 3; r.dimension_to_starboard = 3;
  return r;
}

std::vector<std::string> AisSim::PositionSentences() const {
  std::vector<std::string> out;
  for (const Vessel& v : vessels_) out.push_back(AisEncoder::EncodeAivdmMessage1(ToReport(v)));
  return out;
}

std::vector<std::string> AisSim::StaticSentences() const {
  std::vector<std::string> out;
  for (const Vessel& v : vessels_) out.push_back(AisEncoder::EncodeAivdmMessage5(ToReport(v)));
  return out;  // type 5 is two sentences separated by '\n'
}

}  // namespace radarsim
