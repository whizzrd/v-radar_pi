#include "clutter.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace radarsim {
namespace {
// Echoes add as power, not as levels: a target inside a rain cell of about the same strength is hardly
// distinguishable from it, and heavy rain hides small targets. One level is about 2 dB.
double PowerSum(double a, double b) {
  const double k = 2.0 / 10.0 * std::log(10.0);  // ln of the power ratio per level
  double pa = a > 0.01 ? std::exp(a * k) : 0.0, pb = b > 0.01 ? std::exp(b * k) : 0.0;
  double p = pa + pb;
  return p > 1.0 ? std::log(p) / k : 0.0;
}
const double kPi = 3.14159265358979323846;

struct Rng {
  uint32_t s;
  explicit Rng(uint32_t seed) : s(seed * 2654435761u + 0x9e3779b9u) { if (!s) s = 1; for (int i = 0; i < 3; i++) Next(); }
  uint32_t Next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
  double U() { return (Next() >> 8) / 16777216.0; }  // [0,1)
  double Exp() { return -std::log(1.0 - U()); }       // mean 1
};
}  // namespace

void Clutter::Apply(const OwnShip& ship, double bearing_deg, double range_m, uint32_t scan, const WeatherState& w,
                    const RadarControls& c, uint8_t* out, int n, const uint8_t* land) {
  Rng rng(scan);
  const double step = range_m / n;
  const double br = bearing_deg * kPi / 180.0;
  const double dx = std::sin(br), dy = std::cos(br);
  const double mx = 111320.0 * std::cos(ship.lat * kPi / 180.0), my = 110574.0;

  // --- controls ---
  const double gain_frac = (c.gain_auto ? 55 : std::min(100, std::max(0, c.gain_pct))) / 100.0;
  // Gain adds or removes receiver amplification: echo levels are roughly logarithmic, so it is an offset
  // (50% = unity, 0% removes everything but the strongest echoes, 100% saturates and shows noise).
  const double gain_off = (gain_frac - 0.5) * 28.0;
  double sea_frac = std::min(100, std::max(0, c.sea_pct)) / 100.0;
  if (c.sea_mode == 1) sea_frac = std::max(sea_frac, 0.35);       // harbour: moderate suppression
  else if (c.sea_mode == 2) sea_frac = std::max(sea_frac, 0.6);   // offshore: stronger
  const double ftc = std::min(100, std::max(0, c.rain_pct)) / 100.0;
  const double nr = std::min(3, std::max(0, c.noise_rejection));
  const double boost = std::min(2, std::max(0, c.target_boost));
  const double rej_frac = std::min(1.0, (std::max(0, c.interference_rejection) + std::max(0, c.local_interference_rejection)) / 4.0);

  // --- sea clutter amplitude: grows with waves, strongest upwind, falls off with range ---
  const double wave = std::min(w.wave_m, 4.0);
  const double base = (2.0 + 4.0 * wave) * (0.7 + 0.3 * std::cos(br - w.wind_from_deg * kPi / 180.0));
  const double r0 = 200.0, expo = 1.4;

  double speck = rng.Exp();
  double rain_prev = 0;
  for (int i = 0; i < n; i++) {
    const double r = (i + 0.5) * step;
    const double sig = out[i];

    // sea clutter: exponentially distributed speckle, correlated over a few samples
    double sea = 0;
    if ((!land || !land[i]) && base > 0.1) {
      speck = 0.55 * speck + 0.45 * rng.Exp() * 1.8;
      sea = base * std::pow(r0 / std::max(r, 30.0), expo) * speck;
    }
    // STC: sensitivity cut near the ship, fading out by 3 km
    const double stc = sea_frac * std::max(0.0, 1.0 - r / 3000.0);
    sea *= (1.0 - stc);

    // rain / squalls: evaluated every few samples and interpolated (the field is smooth)
    double rain;
    if (i % 8 == 0) {
      double e = dx * (r + 4 * step), nn = dy * (r + 4 * step);
      double rate = RainRate(w, ship.lat + nn / my, ship.lon + e / mx);
      double dbz = rate > 0.01 ? 10.0 * std::log10(200.0 * std::pow(rate, 1.6)) : 0.0;  // Z = 200 R^1.6
      rain_prev = rate > 0.01 ? std::min(14.0, std::max(0.0, (dbz - 17.0) / 2.4)) : 0.0;
    }
    rain = rain_prev;
    if (rain > 0) rain *= std::min(1.0, std::pow(4000.0 / std::max(r, 100.0), 0.5)) * (0.55 + 0.9 * rng.U());
    rain *= (1.0 - ftc);                      // FTC differentiates the rain echo away
    sea *= (1.0 - 0.2 * ftc);

    // receiver noise: only visible once gain lifts it; noise rejection (a video filter) thins it out
    // (sparse speckle: exponentially distributed, above the display threshold only at high gain)
    const double noise = std::max(0.0, rng.Exp() * 1.1 + gain_off - 9.5) * (1.0 - 0.27 * nr);
    // real echoes are weakened by STC close in, and by FTC when it is turned up
    double tgt = sig * (1.0 - 0.6 * stc) * (1.0 - 0.35 * ftc * ftc);
    if (sig > 0.5) tgt += boost * 1.6 * std::max(0.0, 1.0 - sig / 9.0);  // target boost lifts weak echoes
    const double echo = PowerSum(tgt, PowerSum(sea, rain));
    // gain amplifies echoes (very weak ones, far below the noise floor, hardly at all); the noise floor is separate
    double v = PowerSum(echo > 0.05 ? std::max(0.0, echo + gain_off * std::min(1.0, echo / 2.0)) : 0.0, noise);
    if (v < 5.0) v -= 0.4 * nr;  // ... at the price of weak real echoes
    out[i] = (uint8_t)std::min(15.0, std::max(0.0, std::floor(v + 0.5)));
  }
  // pulse length and the Target expansion / Target separation controls: echoes are stretched in range
  // (~25 m per step; separation sharpens, expansion widens)
  {
    const int units = std::max(0, 1 + std::min(3, std::max(0, c.target_expansion)) - std::min(3, std::max(0, c.target_separation)));
    const int wsamp = (int)std::lround(units * 25.0 / step);
    if (wsamp > 0) {
      static thread_local uint8_t tmp[4096];
      if (n <= 4096) {
        std::memcpy(tmp, out, n);
        for (int i = 0; i < n; i++) {
          if (tmp[i] < 2) continue;
          int a = std::max(0, i - wsamp), b = std::min(n - 1, i + wsamp);
          for (int j = a; j <= b; j++) out[j] = std::max(out[j], tmp[i]);
        }
      }
    }
  }

  // interference from other radars: dotted radial streaks; the rejection controls remove most of them
  if (w.interference > 0) {
    const double p = (w.interference == 1 ? 0.015 : 0.06) * (1.0 - 0.85 * rej_frac);
    if (rng.U() < p) {
      int start = (int)(n * (0.05 + 0.8 * rng.U())), stride = 5 + (int)(rng.U() * 9), len = 15 + (int)(rng.U() * 60);
      double lvl = std::min(15.0, std::max(0.0, 7.0 + gain_off));
      for (int k = 0; k < len; k++) {
        int j = start + k * stride;
        if (j >= n) break;
        out[j] = std::max<uint8_t>(out[j], (uint8_t)lvl);
      }
    }
  }
}

}  // namespace radarsim
