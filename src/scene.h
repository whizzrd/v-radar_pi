#pragma once
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "s57_reader.h"

namespace radarsim {

struct OwnShip {
  double lat = 0, lon = 0;
  double heading_deg = 0;  // true
};

// A world that can be "seen" by the simulated radar.
class Scene {
 public:
  // Moving point echo (e.g. an AIS vessel); lat/lon in degrees, size in metres.
  struct Target { double lat, lon; double size_m; uint8_t level; };
  virtual ~Scene() = default;
  virtual void SetTargets(std::vector<Target>) {}
  // True once the scene knows the land layout, so IsWater() can be trusted.
  virtual bool Ready() const { return true; }
  // False when the point lies on land (inside a land polygon of the ENC extract).
  virtual bool IsWater(double /*lat*/, double /*lon*/) const { return true; }
  // Fill out[0..n) with echo intensity 0..15 for one spoke. Sample i covers
  // distance (i + 0.5) * range_m / n from the ship along bearing_deg (true).
  // If land is non-null it receives 1 for every sample that lies inside land (the receiver model uses it
  // to keep sea clutter off the land).
  virtual void RenderSpoke(const OwnShip& ship, double bearing_deg,
                           double range_m, uint8_t* out, int n, uint8_t* land = nullptr) = 0;
};

// Small synthetic world around the ship: a land mass, a breakwater, buoys and a
// moving vessel. Used to prove the protocol before real chart data is wired in.
class TestScene : public Scene {
 public:
  void RenderSpoke(const OwnShip& ship, double bearing_deg, double range_m,
                   uint8_t* out, int n, uint8_t* land = nullptr) override;
};

// Radar picture computed from an ENC extract (see tools/enc_extract.py): land edges,
// shoreline reflectors and navigation marks, ray-cast per spoke.
class EncScene : public Scene {
 public:
  // Returns false (and sets error) when the file cannot be read.
  bool Load(const std::string& path, std::string& error);
  // Takes the features read natively from the ENC cells (see s57_reader.h).
  void SetData(const EncData& data);
  // Moving targets (e.g. AIS) are drawn as point echoes; lat/lon in degrees.
  void SetTargets(std::vector<Target> targets) override;
  bool Ready() const override { return !geo_segs_.empty(); }
  bool IsWater(double lat, double lon) const override;
  void RenderSpoke(const OwnShip& ship, double bearing_deg, double range_m,
                   uint8_t* out, int n, uint8_t* land = nullptr) override;

 private:
  struct Seg { float x1, y1, x2, y2; uint8_t kind; uint16_t layer; };  // kind: 0 land edge, 1 border (parity only), 2 reflector
  struct Pt { float x, y; uint8_t level; };
  void Rebuild(double lat0, double lon0);
  void ToEnu(double lat, double lon, double& x, double& y) const;

  struct Geo { double lon1, lat1, lon2, lat2; uint8_t kind; uint16_t layer; };
  struct GeoPt { double lon, lat; uint8_t level; };
  void BuildLandIndex();
  std::vector<EncData::Layer> layers_;
  struct LayerBox { double x0, y0, x1, y1; };  // layer extents in the local frame (see Rebuild)
  std::vector<LayerBox> layer_boxes_;
  // land edges bucketed by latitude strip so that IsWater() does not scan every edge
  std::vector<std::vector<int>> strips_;
  double strip_lat0_ = 0, strip_dlat_ = 1;
  std::vector<Geo> geo_segs_;
  std::vector<GeoPt> geo_pts_;
  std::vector<Seg> segs_;
  std::vector<Pt> pts_;
  std::vector<std::vector<int>> grid_;
  int gw_ = 0, gh_ = 0;
  double lat0_ = 0, lon0_ = 0, kx_ = 0, ky_ = 110574.0, half_ = 0;
  bool have_ref_ = false;
  std::vector<Target> targets_;
  std::mutex targets_mu_;
  std::vector<int> stamp_;
  int stamp_now_ = 0;
};

// Lets the scene be swapped while the radar thread keeps rendering (e.g. new ENC region).
class SwitchableScene : public Scene {
 public:
  void Set(std::shared_ptr<Scene> s) {
    std::lock_guard<std::mutex> l(mu_);
    scene_ = std::move(s);
    if (scene_) scene_->SetTargets(targets_);
  }
  bool Ready() const override {
    std::shared_ptr<Scene> s = Current();
    return s && s->Ready();
  }
  bool IsWater(double lat, double lon) const override {
    std::shared_ptr<Scene> s = Current();
    return s ? s->IsWater(lat, lon) : true;
  }
  void SetTargets(std::vector<Target> t) override {
    std::lock_guard<std::mutex> l(mu_);
    targets_ = t;
    if (scene_) scene_->SetTargets(std::move(t));
  }
  void RenderSpoke(const OwnShip& ship, double bearing_deg, double range_m, uint8_t* out, int n, uint8_t* land = nullptr) override {
    std::shared_ptr<Scene> s;
    { std::lock_guard<std::mutex> l(mu_); s = scene_; }
    if (s) s->RenderSpoke(ship, bearing_deg, range_m, out, n, land);
    else { for (int i = 0; i < n; i++) out[i] = 0; if (land) for (int i = 0; i < n; i++) land[i] = 0; }
  }
 private:
  std::shared_ptr<Scene> Current() const {
    std::lock_guard<std::mutex> l(mu_);
    return scene_;
  }
  mutable std::mutex mu_;
  std::shared_ptr<Scene> scene_;
  std::vector<Target> targets_;
};

}  // namespace radarsim
