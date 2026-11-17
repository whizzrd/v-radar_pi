#include "scene.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

namespace radarsim {
namespace {
const double kPi = 3.14159265358979323846;

struct Disc { double east_m, north_m, radius_m; uint8_t level; };
}  // namespace

void TestScene::RenderSpoke(const OwnShip&, double bearing_deg, double range_m,
                            uint8_t* out, int n, uint8_t* land) {
  if (land) std::memset(land, 0, n);
  std::memset(out, 0, n);
  double t = std::chrono::duration<double>(
                 std::chrono::steady_clock::now().time_since_epoch())
                 .count();
  // moving vessel circling slowly, 0.8 NM east of ship
  double vx = 1500 + 300 * std::cos(t / 20.0), vy = 300 * std::sin(t / 20.0);
  const Disc discs[] = {
      {-1800, 600, 700, 12},  // land to the north-west
      {900, 1100, 25, 15},    // buoy
      {-300, -900, 25, 15},   // buoy
      {vx, vy, 30, 14},       // vessel
  };
  const double br = bearing_deg * kPi / 180.0;
  const double dx = std::sin(br), dy = std::cos(br);
  const double step = range_m / n;
  for (const Disc& d : discs) {
    // intersect ray (from origin along dx,dy) with the disc
    double b = dx * d.east_m + dy * d.north_m;
    double c = d.east_m * d.east_m + d.north_m * d.north_m - d.radius_m * d.radius_m;
    double disc = b * b - c;
    if (disc < 0) continue;
    double s = std::sqrt(disc);
    double r0 = b - s, r1 = b + s;
    if (r1 < 0) continue;
    int i0 = std::max(0, (int)(r0 / step)), i1 = std::min(n - 1, (int)(r1 / step));
    for (int i = i0; i <= i1; i++) {
      // land: strong at the near edge only (radar shadow behind the coast)
      bool land = d.radius_m > 100;
      int level = land ? (i - i0 < 6 ? d.level : 3) : d.level;
      out[i] = std::max<uint8_t>(out[i], (uint8_t)level);
    }
  }
}

}  // namespace radarsim

// ---------------------------------------------------------------------------
// EncScene
// ---------------------------------------------------------------------------
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <fstream>

namespace radarsim {
namespace {
const double kCell = 400.0;       // grid cell size in metres
const double kHalfExtent = 24000; // grid covers +-24 km around the reference point
const double kReloadDrift = 4000; // rebuild ENU grid when the ship drifted this far (m)
const double kBeamHalf = 0.6 * kPi / 180.0;

uint8_t PointLevel(const std::string& kind) {
  if (kind == "LNDMRK") return 13;
  if (kind.compare(0, 3, "BCN") == 0) return 12;
  if (kind.compare(0, 3, "BOY") == 0) return 11;
  if (kind == "PILPNT") return 6;
  return 5;  // MORFAC and other small fixtures
}
}  // namespace

bool EncScene::Load(const std::string& path, std::string& error) {
  std::ifstream in(path);
  if (!in) { error = "cannot open " + path; return false; }
  geo_segs_.clear(); geo_pts_.clear(); layers_.clear();
  layers_.push_back({-1e9, -1e9, 1e9, 1e9, 0});  // a text extract is one layer without a known extent
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::istringstream ss(line);
    std::string k; ss >> k;
    if (k == "L" || k == "E") {
      Geo g; std::string flag;
      ss >> g.lon1 >> g.lat1 >> g.lon2 >> g.lat2 >> flag;
      g.kind = (k == "E") ? 2 : (flag == "X" ? 1 : 0);
      g.layer = 0;
      geo_segs_.push_back(g);
    } else if (k == "P") {
      GeoPt p; std::string kind;
      ss >> p.lon >> p.lat >> kind;
      p.level = PointLevel(kind);
      geo_pts_.push_back(p);
    }
  }
  have_ref_ = false;
  if (geo_segs_.empty() && geo_pts_.empty()) { error = "no features in " + path; return false; }
  BuildLandIndex();
  return true;
}

void EncScene::SetData(const EncData& d) {
  geo_segs_.clear(); geo_pts_.clear();
  layers_ = d.layers;
  if (layers_.empty()) layers_.push_back({-1e9, -1e9, 1e9, 1e9, 0});
  geo_segs_.reserve(d.segs.size());
  for (const auto& s : d.segs) geo_segs_.push_back({s.lon1, s.lat1, s.lon2, s.lat2, s.kind, s.layer});
  for (const auto& p : d.pts) geo_pts_.push_back({p.lon, p.lat, p.level});
  have_ref_ = false;
  BuildLandIndex();
}

void EncScene::BuildLandIndex() {
  strips_.clear();
  double lo = 1e9, hi = -1e9;
  for (const Geo& g : geo_segs_) {
    if (g.kind == 2) continue;
    lo = std::min(lo, std::min(g.lat1, g.lat2)); hi = std::max(hi, std::max(g.lat1, g.lat2));
  }
  if (hi <= lo) return;
  const int kStrips = 1024;
  strip_lat0_ = lo; strip_dlat_ = (hi - lo) / kStrips;
  strips_.assign(kStrips, {});
  for (size_t i = 0; i < geo_segs_.size(); i++) {
    const Geo& g = geo_segs_[i];
    if (g.kind == 2) continue;
    int a = (int)((std::min(g.lat1, g.lat2) - lo) / strip_dlat_), b = (int)((std::max(g.lat1, g.lat2) - lo) / strip_dlat_);
    for (int k = std::max(0, a); k <= std::min(kStrips - 1, b); k++) strips_[k].push_back((int)i);
  }
}

bool EncScene::IsWater(double lat, double lon) const {
  // Even-odd test per layer (= per ENC cell): count the land edges an eastward ray crosses; odd means
  // inside that cell's land. The layers are united, so overlapping cells of different bands never cancel.
  if (strips_.empty()) return true;
  int k = (int)((lat - strip_lat0_) / strip_dlat_);
  if (k < 0 || k >= (int)strips_.size()) return true;
  std::vector<char> odd(layers_.size(), 0);
  for (int idx : strips_[k]) {
    const Geo& g = geo_segs_[idx];
    if ((g.lat1 > lat) == (g.lat2 > lat)) continue;
    double x = g.lon1 + (lat - g.lat1) * (g.lon2 - g.lon1) / (g.lat2 - g.lat1);
    if (x > lon) odd[g.layer] ^= 1;
  }
  for (size_t l = 0; l < layers_.size(); l++) {
    if (!odd[l]) continue;
    const auto& L = layers_[l];
    if (L.urban) continue;  // built-up areas lie on land; they do not decide land versus water
    if (lon >= L.lon_min && lon <= L.lon_max && lat >= L.lat_min && lat <= L.lat_max) return false;
  }
  return true;
}

void EncScene::SetTargets(std::vector<Target> t) {
  std::lock_guard<std::mutex> lock(targets_mu_);
  targets_ = std::move(t);
}

void EncScene::ToEnu(double lat, double lon, double& x, double& y) const {
  x = (lon - lon0_) * kx_;
  y = (lat - lat0_) * ky_;
}

void EncScene::Rebuild(double lat0, double lon0) {
  lat0_ = lat0; lon0_ = lon0;
  kx_ = 111320.0 * std::cos(lat0 * kPi / 180.0);
  half_ = kHalfExtent;
  gw_ = gh_ = (int)(2 * half_ / kCell) + 1;
  grid_.assign((size_t)gw_ * gh_, {});
  segs_.clear(); pts_.clear();
  layer_boxes_.clear();
  for (const auto& L : layers_) {
    LayerBox b;
    ToEnu(L.lat_min, L.lon_min, b.x0, b.y0); ToEnu(L.lat_max, L.lon_max, b.x1, b.y1);
    layer_boxes_.push_back(b);
  }
  for (const Geo& g : geo_segs_) {
    double x1, y1, x2, y2;
    ToEnu(g.lat1, g.lon1, x1, y1); ToEnu(g.lat2, g.lon2, x2, y2);
    if (std::max(std::fabs(x1), std::fabs(x2)) > half_ || std::max(std::fabs(y1), std::fabs(y2)) > half_) continue;
    int id = (int)segs_.size();
    segs_.push_back({(float)x1, (float)y1, (float)x2, (float)y2, g.kind, g.layer});
    // register in every grid cell the segment's bounding box touches (segments are short)
    int cx1 = (int)((std::min(x1, x2) + half_) / kCell), cx2 = (int)((std::max(x1, x2) + half_) / kCell);
    int cy1 = (int)((std::min(y1, y2) + half_) / kCell), cy2 = (int)((std::max(y1, y2) + half_) / kCell);
    for (int cy = std::max(0, cy1); cy <= std::min(gh_ - 1, cy2); cy++)
      for (int cx = std::max(0, cx1); cx <= std::min(gw_ - 1, cx2); cx++)
        grid_[(size_t)cy * gw_ + cx].push_back(id);
  }
  for (const GeoPt& p : geo_pts_) {
    double x, y; ToEnu(p.lat, p.lon, x, y);
    if (std::fabs(x) <= half_ && std::fabs(y) <= half_) pts_.push_back({(float)x, (float)y, p.level});
  }
  stamp_.assign(segs_.size(), 0);
  stamp_now_ = 0;
  have_ref_ = true;
}

void EncScene::RenderSpoke(const OwnShip& ship, double bearing_deg, double range_m, uint8_t* out, int n, uint8_t* land) {
  std::memset(out, 0, n);
  if (land) std::memset(land, 0, n);
  if (!have_ref_ || std::fabs((ship.lat - lat0_) * ky_) > kReloadDrift ||
      std::fabs((ship.lon - lon0_) * kx_) > kReloadDrift)
    Rebuild(ship.lat, ship.lon);
  double sx, sy; ToEnu(ship.lat, ship.lon, sx, sy);
  const double br = bearing_deg * kPi / 180.0;
  const double dx = std::sin(br), dy = std::cos(br);
  const double step = range_m / n;
  auto mark = [&](double r, int level, int width) {
    int i = (int)(r / step);
    for (int k = 0; k < width; k++) {
      int j = i + k;
      if (j >= 0 && j < n) out[j] = std::max<uint8_t>(out[j], (uint8_t)level);
    }
  };

  // --- land edges and reflectors: collect ray/segment intersections ---
  struct Hit { double t; uint8_t kind; uint16_t layer; };
  static thread_local std::vector<Hit> hits;
  hits.clear();
  stamp_now_++;
  const double ex = sx + dx * range_m, ey = sy + dy * range_m;
  const int steps = (int)(range_m / (kCell * 0.5)) + 1;
  for (int s = 0; s <= steps; s++) {
    double px = sx + (ex - sx) * s / steps, py = sy + (ey - sy) * s / steps;
    int cx = (int)((px + half_) / kCell), cy = (int)((py + half_) / kCell);
    for (int oy = -1; oy <= 1; oy++) for (int ox = -1; ox <= 1; ox++) {
      int gx = cx + ox, gy = cy + oy;
      if (gx < 0 || gy < 0 || gx >= gw_ || gy >= gh_) continue;
      for (int id : grid_[(size_t)gy * gw_ + gx]) {
        if (stamp_[id] == stamp_now_) continue;
        stamp_[id] = stamp_now_;
        const Seg& g = segs_[id];
        double rx = g.x2 - g.x1, ry = g.y2 - g.y1;
        double den = dx * ry - dy * rx;
        if (std::fabs(den) < 1e-9) continue;
        double qx = g.x1 - sx, qy = g.y1 - sy;
        double t = (qx * ry - qy * rx) / den;
        double u = (qx * dy - qy * dx) / den;
        if (t >= 0 && t <= range_m && u >= 0 && u < 1) hits.push_back({t, g.kind, g.layer});
      }
    }
  }
  std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.t < b.t; });

  // Each layer (ENC cell) keeps its own inside/outside state; the layers are united. A shoreline of a
  // coarser cell is not drawn where a finer cell covers the same spot.
  const size_t nl = layers_.size();
  static thread_local std::vector<char> inside;
  static thread_local std::vector<double> entered, land_start;
  inside.assign(nl, 0); entered.assign(nl, -1.0); land_start.assign(nl, -1.0);
  auto superseded = [&](const Hit& h) {
    if (nl < 2) return false;
    double px = sx + dx * h.t, py = sy + dy * h.t;
    const int band = layers_[h.layer].band;
    for (size_t m = 0; m < nl; m++) {
      if (layers_[m].band <= band) continue;
      const LayerBox& b = layer_boxes_[m];
      if (px >= b.x0 && px <= b.x1 && py >= b.y0 && py <= b.y1) return true;
    }
    return false;
  };
  auto fill_body = [&](size_t l, double end) {
    if (entered[l] < 0) return;
    // bare land: weak echo, ~250 m deep (radar shadow); built-up areas return strongly and speckled, ~600 m deep
    const bool urban = layers_[l].urban;
    int a = (int)(entered[l] / step), b = (int)(std::min(end, entered[l] + (urban ? 600.0 : 250.0)) / step);
    for (int i = std::max(0, a); i <= b && i < n; i++) {
      int level = urban ? 6 + (int)(((uint32_t)(i * 2654435761u + (uint32_t)(bearing_deg * 10)) >> 13) & 3) : 3;
      out[i] = std::max<uint8_t>(out[i], (uint8_t)level);
    }
  };
  auto mark_land = [&](size_t l, double end) {
    if (!land || land_start[l] < 0 || layers_[l].urban) return;
    int a = (int)(land_start[l] / step), b = (int)(end / step);
    for (int i = std::max(0, a); i <= b && i < n; i++) land[i] = 1;
  };
  for (const Hit& h : hits) {
    if (h.kind == 2) { if (!superseded(h)) mark(h.t, 9, 2); continue; }
    const size_t l = h.layer;
    inside[l] = !inside[l];  // land polygon boundary (kind 0 or 1) toggles inside/outside
    if (inside[l]) {
      land_start[l] = h.t;
      entered[l] = (h.kind == 0) ? h.t : -1.0;  // entered across a cell border: no shoreline, no new land body
      if (h.kind == 0 && !layers_[l].urban && !superseded(h)) mark(h.t, 14, 2);
    } else {
      if (h.kind == 0 && !layers_[l].urban && !superseded(h)) mark(h.t, 14, 2);
      fill_body(l, h.t);
      mark_land(l, h.t);
      entered[l] = -1.0; land_start[l] = -1.0;
    }
  }
  for (size_t l = 0; l < nl; l++) if (inside[l]) { fill_body(l, range_m); mark_land(l, range_m); }

  // --- point targets (marks) ---
  for (const Pt& p : pts_) {
    double px = p.x - sx, py = p.y - sy;
    double r = px * dx + py * dy;
    if (r <= 0 || r > range_m) continue;
    double perp = std::fabs(px * dy - py * dx);
    if (perp < std::max(10.0, r * std::tan(kBeamHalf))) mark(r, p.level, 2);
  }
  std::vector<Target> targets;
  { std::lock_guard<std::mutex> lock(targets_mu_); targets = targets_; }
  for (const Target& t : targets) {
    double x, y; ToEnu(t.lat, t.lon, x, y);
    double px = x - sx, py = y - sy;
    double r = px * dx + py * dy;
    if (r <= 0 || r > range_m) continue;
    double perp = std::fabs(px * dy - py * dx);
    if (perp < std::max(t.size_m, r * std::tan(kBeamHalf))) mark(r, t.level, 2 + (int)(t.size_m / step));
  }
}

}  // namespace radarsim
