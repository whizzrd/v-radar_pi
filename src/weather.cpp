#include "weather.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace radarsim {
namespace {
const double kPi = 3.14159265358979323846;

// Cheap 2-D value noise in [0,1].
double Hash(int x, int y) {
  uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return ((h ^ (h >> 16)) & 0xffffff) / 16777215.0;
}
double Noise(double x, double y) {
  int xi = (int)std::floor(x), yi = (int)std::floor(y);
  double fx = x - xi, fy = y - yi;
  fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
  double a = Hash(xi, yi), b = Hash(xi + 1, yi), c = Hash(xi, yi + 1), d = Hash(xi + 1, yi + 1);
  return (a + (b - a) * fx) + ((c + (d - c) * fx) - (a + (b - a) * fx)) * fy;
}

double GridRate(const WeatherState::Grid& g, double lat, double lon) {
  if (g.empty()) return 0;
  double fx = (lon - g.lon0) / g.dlon, fy = (lat - g.lat0) / g.dlat;
  if (fx < 0 || fy < 0 || fx > g.nx - 1 || fy > g.ny - 1) return 0;
  int ix = std::min((int)fx, g.nx - 2), iy = std::min((int)fy, g.ny - 2);
  double tx = fx - ix, ty = fy - iy;
  auto v = [&](int x, int y) { return (double)g.rate[(size_t)y * g.nx + x]; };
  return (v(ix, iy) * (1 - tx) + v(ix + 1, iy) * tx) * (1 - ty) + (v(ix, iy + 1) * (1 - tx) + v(ix + 1, iy + 1) * tx) * ty;
}
}  // namespace

double RainRate(const WeatherState& w, double lat, double lon) {
  const double mx = 111320.0 * std::cos(lat * kPi / 180.0), my = 110574.0;
  // position in the drifting frame of the rain pattern
  const double e = lon * mx - w.drift_e, n = lat * my - w.drift_n;
  double rate = 0;

  double base = GridRate(w.grid, lat - w.drift_n / my, lon - w.drift_e / mx);
  if (base > 0.02) {
    // GRIB cells are ~15-30 NM wide: break them up into showers. More convective = patchier and heavier cores.
    double t = 0.6 * Noise(e / 5000.0, n / 5000.0) + 0.4 * Noise(e / 1500.0 + 7, n / 1500.0 + 3);
    double lo = 0.55 - 0.25 * w.convective, hi = lo + 0.25;
    double s = std::min(1.0, std::max(0.0, (t - lo) / (hi - lo)));
    rate += base * (0.15 + (1.0 + 2.0 * w.convective) * s);
  }
  for (const auto& c : w.cells) {
    // cells move with the pattern: compare in the drifting frame, around their stored position
    double cx = c.lon * mx, cy = c.lat * my;
    double dx = (lon * mx) - cx - w.drift_e, dy = (lat * my) - cy - w.drift_n;
    double r2 = (dx * dx + dy * dy) / (c.radius_m * c.radius_m);
    if (r2 < 9) {
      double tex = 0.65 + 0.7 * Noise(e / 600.0, n / 600.0);  // ragged edges and cores
      rate += c.rate_mmh * std::exp(-r2) * tex;
    }
  }
  return rate;
}

std::vector<WeatherState::Cell> MakeRainPreset(int preset, double lat, double lon, unsigned seed) {
  std::vector<WeatherState::Cell> cells;
  if (preset <= 0) return cells;
  std::mt19937 rng(seed * 2654435761u + 17u);
  std::uniform_real_distribution<double> u(0.0, 1.0);
  const double mx = 111320.0 * std::cos(lat * kPi / 180.0), my = 110574.0;
  auto place = [&](double r_m, double ang, double radius, double rate) {
    cells.push_back({lat + r_m * std::cos(ang) / my, lon + r_m * std::sin(ang) / mx, radius, rate});
  };
  if (preset == 1) {                      // light showers: a few broad, weak cells
    for (int i = 0; i < 6; i++) place(3000 + 14000 * u(rng), 2 * kPi * u(rng), 1200 + 1300 * u(rng), 1.0 + 1.5 * u(rng));
  } else if (preset == 2) {               // heavy showers: fewer, stronger cells
    for (int i = 0; i < 5; i++) place(2500 + 12000 * u(rng), 2 * kPi * u(rng), 1000 + 1200 * u(rng), 5 + 8 * u(rng));
  } else {                                // squall line: a row of intense cells across the ship's surroundings
    double ang = 2 * kPi * u(rng);
    double dist = 6000 + 4000 * u(rng);
    double px = std::sin(ang), py = std::cos(ang);       // direction towards the line
    double ex = std::cos(ang), ey = -std::sin(ang);      // along the line
    for (int i = -4; i <= 4; i++) {
      double along = i * 3500.0 + 800 * (u(rng) - 0.5);
      double cx = px * dist + ex * along, cy = py * dist + ey * along;
      cells.push_back({lat + cy / my, lon + cx / mx, 1300 + 900 * u(rng), 20 + 25 * u(rng)});
    }
  }
  return cells;
}

}  // namespace radarsim
