#include "s57_reader.h"

#include <sys/stat.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <map>
#include <sstream>
#include <unordered_map>

namespace radarsim {
namespace {

// ---- S-57 object classes we care about (codes from the IHO object catalogue) ----
enum : int {
  BCNCAR = 5, BCNISD = 6, BCNLAT = 7, BCNSAW = 8, BCNSPP = 9, BRIDGE = 11, BUISGL = 12, BUAARE = 13,
  CAUSWY = 26, CRANES = 35, DYKCON = 49, FLODOC = 57, GATCON = 61, HULKES = 65, OFSPLF = 87, PYLONS = 98, RADSTA = 102, SILTNK = 125,
  BOYCAR = 14, BOYISD = 16, BOYLAT = 17, BOYSAW = 18, BOYSPP = 19,
  COALNE = 30, DAMCON = 38, LNDARE = 71, LNDMRK = 74, MORFAC = 84, PILPNT = 90,
  PONTON = 95, SLCONS = 122,
};
// (hard-coded object class codes of the IHO S-57 object catalogue)

bool IsLineObject(int o) {
  return o == COALNE || o == SLCONS || o == BRIDGE || o == DAMCON || o == PONTON || o == CAUSWY || o == DYKCON ||
         o == FLODOC || o == GATCON;
}

// Echo strength of a point object (same scale as the Python extractor used).
int PointLevel(int o) {
  switch (o) {
    case LNDMRK: return 13;
    case BCNCAR: case BCNISD: case BCNLAT: case BCNSAW: case BCNSPP: return 12;
    case BOYCAR: case BOYISD: case BOYLAT: case BOYSAW: case BOYSPP: return 11;
    case PILPNT: return 6;
    case MORFAC: return 5;
    case BUISGL: return 8;    // single building
    case SILTNK: return 10;   // silo, tank: strong reflector
    case CRANES: return 11;   // crane
    case PYLONS: return 9;    // pylon, bridge pier
    case HULKES: return 12;   // hulk
    case OFSPLF: return 13;   // offshore platform
    case RADSTA: return 10;   // radar station/tower
    default: return -1;
  }
}

uint32_t Le32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
uint16_t Le16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
int32_t LeS32(const uint8_t* p) { return (int32_t)Le32(p); }

long Digits(const uint8_t* p, int n) {
  long v = 0;
  for (int i = 0; i < n; i++) {
    if (p[i] < '0' || p[i] > '9') return -1;
    v = v * 10 + (p[i] - '0');
  }
  return v;
}

struct Field { std::string tag; const uint8_t* data; size_t len; };

// Minimal ISO 8211 reader: walks the records and exposes their fields. The field formats of
// S-57 are fixed, so the data descriptive record is skipped and the layouts are built in.
class Iso8211 {
 public:
  bool Load(const std::string& path, std::string& err) {
    std::ifstream in(path, std::ios::binary);
    if (!in) { err = "cannot open " + path; return false; }
    in.seekg(0, std::ios::end);
    std::streamoff size = in.tellg();
    in.seekg(0);
    buf_.resize((size_t)size);
    if (size > 0) in.read((char*)buf_.data(), size);
    return true;
  }
  // Next data record (the leading descriptive record is skipped). Returns false at the end.
  bool Next(std::vector<Field>& fields, std::string& err) {
    for (;;) {
      fields.clear();
      if (pos_ + 24 > buf_.size()) return false;
      const uint8_t* l = &buf_[pos_];
      long len = Digits(l, 5);
      long base = Digits(l + 12, 5);
      int size_len = l[20] - '0', size_pos = l[21] - '0', size_tag = l[23] - '0';
      if (len < 24 || pos_ + (size_t)len > buf_.size() || base < 24 || base > len || size_len < 1 ||
          size_pos < 1 || size_tag < 1) {
        err = "malformed ISO 8211 record";
        return false;
      }
      const bool ddr = l[6] == 'L';
      const uint8_t* rec = l;
      size_t d = 24;
      const size_t entry = (size_t)(size_tag + size_len + size_pos);
      while (d + entry <= (size_t)base && rec[d] != 0x1E) {
        Field f;
        f.tag.assign((const char*)rec + d, (size_t)size_tag);
        long flen = Digits(rec + d + size_tag, size_len);
        long foff = Digits(rec + d + size_tag + size_len, size_pos);
        d += entry;
        if (flen < 0 || foff < 0 || (size_t)(base + foff + flen) > (size_t)len) { err = "bad directory entry"; return false; }
        f.data = rec + base + foff;
        f.len = (size_t)flen;
        if (f.len > 0 && f.data[f.len - 1] == 0x1E) f.len--;
        fields.push_back(f);
      }
      pos_ += (size_t)len;
      if (ddr) continue;
      return true;
    }
  }

 private:
  std::vector<uint8_t> buf_;
  size_t pos_ = 0;
};

struct Vec {
  std::vector<std::pair<double, double>> v;  // lon, lat
  uint8_t brcnm = 0, ercnm = 0;
  uint32_t b = 0, e = 0;
  bool hb = false, he = false;
};
struct Fsp { uint8_t rcnm; uint32_t rcid; uint8_t ornt, usag, mask; };
struct Feat { int prim; int objl; std::vector<Fsp> fs; };

uint64_t Key(int rcnm, uint32_t rcid) { return ((uint64_t)rcnm << 32) | rcid; }

struct Cell {
  double comf = 1e7;
  std::unordered_map<uint64_t, Vec> vecs;
  std::vector<Feat> feats;
  GeoBox ext{1e9, 1e9, -1e9, -1e9};
  bool have_ext = false;
  void Grow(double lon, double lat) {
    ext.lon_min = std::min(ext.lon_min, lon); ext.lon_max = std::max(ext.lon_max, lon);
    ext.lat_min = std::min(ext.lat_min, lat); ext.lat_max = std::max(ext.lat_max, lat);
    have_ext = true;
  }
};

bool ParseCell(const std::string& path, bool want_features, Cell& c, std::string& err) {
  Iso8211 f;
  if (!f.Load(path, err)) return false;
  std::vector<Field> fields;
  std::string rerr;
  int records = 0;
  while (f.Next(fields, rerr)) {
    records++;
    const Field *dspm = nullptr, *vrid = nullptr, *frid = nullptr, *sg2d = nullptr, *vrpt = nullptr, *fspt = nullptr;
    for (const Field& fl : fields) {
      if (fl.tag == "DSPM") dspm = &fl;
      else if (fl.tag == "VRID") vrid = &fl;
      else if (fl.tag == "FRID") frid = &fl;
      else if (fl.tag == "SG2D") sg2d = &fl;
      else if (fl.tag == "VRPT") vrpt = &fl;
      else if (fl.tag == "FSPT") fspt = &fl;
    }
    if (dspm && dspm->len >= 20) {
      uint32_t comf = Le32(dspm->data + 16);
      if (comf > 0) c.comf = comf;
    } else if (vrid && vrid->len >= 5) {
      int rcnm = vrid->data[0];
      uint32_t rcid = Le32(vrid->data + 1);
      Vec v;
      if (sg2d) {
        for (size_t i = 0; i + 8 <= sg2d->len; i += 8) {
          double lat = LeS32(sg2d->data + i) / c.comf, lon = LeS32(sg2d->data + i + 4) / c.comf;
          v.v.push_back({lon, lat});
          c.Grow(lon, lat);
        }
      }
      if (vrpt) {
        for (size_t i = 0; i + 9 <= vrpt->len; i += 9) {
          const uint8_t* p = vrpt->data + i;
          uint32_t id = Le32(p + 1);
          if (p[7] == 1) { v.b = id; v.brcnm = p[0]; v.hb = true; }
          else if (p[7] == 2) { v.e = id; v.ercnm = p[0]; v.he = true; }
        }
      }
      c.vecs[Key(rcnm, rcid)] = std::move(v);
    } else if (frid && want_features && frid->len >= 12) {
      Feat ft;
      ft.prim = frid->data[5];
      ft.objl = Le16(frid->data + 7);
      if (fspt) {
        for (size_t i = 0; i + 8 <= fspt->len; i += 8) {
          const uint8_t* p = fspt->data + i;
          ft.fs.push_back({p[0], Le32(p + 1), p[5], p[6], p[7]});
        }
      }
      int o = ft.objl;
      if (o == LNDARE || o == BUAARE || IsLineObject(o) || PointLevel(o) >= 0) c.feats.push_back(std::move(ft));
    }
  }
  if (!rerr.empty() && records == 0) { err = rerr; return false; }
  return true;
}

// Full polyline of an edge: begin node, internal vertices, end node.
void EdgePolyline(const Cell& c, uint32_t rcid, std::vector<std::pair<double, double>>& out) {
  out.clear();
  auto it = c.vecs.find(Key(130, rcid));
  if (it == c.vecs.end()) return;
  const Vec& e = it->second;
  auto node = [&](bool has, uint8_t rcnm, uint32_t id) {
    if (!has) return;
    auto n = c.vecs.find(Key(rcnm, id));
    if (n != c.vecs.end() && !n->second.v.empty()) out.push_back(n->second.v[0]);
  };
  node(e.hb, e.brcnm, e.b);
  out.insert(out.end(), e.v.begin(), e.v.end());
  node(e.he, e.ercnm, e.e);
}

bool Overlaps(const GeoBox& a, const GeoBox& b) {
  return !(a.lon_max < b.lon_min || a.lon_min > b.lon_max || a.lat_max < b.lat_min || a.lat_min > b.lat_max);
}

bool IsCloudPlaceholder(const struct stat& st) {
#ifdef __APPLE__
  return (st.st_flags & 0x40000000) != 0;  // SF_DATALESS
#else
  (void)st;
  return false;
#endif
}

}  // namespace

bool S57CellExtent(const std::string& path, GeoBox& box, std::string& err) {
  Cell c;
  if (!ParseCell(path, false, c, err)) return false;
  if (!c.have_ext) { err = "no geometry in " + path; return false; }
  box = c.ext;
  return true;
}

bool ReadS57Cell(const std::string& path, const GeoBox& box, int layer, int urban_layer, EncData& out, std::string& err) {
  Cell c;
  if (!ParseCell(path, true, c, err)) return false;
  std::vector<std::pair<double, double>> pl;
  for (const Feat& ft : c.feats) {
    const int o = ft.objl;
    if (o == LNDARE || o == BUAARE || IsLineObject(o)) {
      std::vector<EncData::Seg> segs;
      GeoBox fb{1e9, 1e9, -1e9, -1e9};
      for (const Fsp& s : ft.fs) {
        if (s.rcnm != 130) continue;
        EdgePolyline(c, s.rcid, pl);
        const bool area = (o == LNDARE || o == BUAARE);
        const uint8_t kind = area ? (s.usag == 3 ? 1 : 0) : 2;
        for (size_t i = 0; i + 1 < pl.size(); i++) {
          segs.push_back({pl[i].first, pl[i].second, pl[i + 1].first, pl[i + 1].second, kind, (uint16_t)(o == BUAARE ? urban_layer : layer)});
        }
        for (auto& p : pl) {
          fb.lon_min = std::min(fb.lon_min, p.first); fb.lon_max = std::max(fb.lon_max, p.first);
          fb.lat_min = std::min(fb.lat_min, p.second); fb.lat_max = std::max(fb.lat_max, p.second);
        }
      }
      if (segs.empty() || !Overlaps(fb, box)) continue;
      if (o == LNDARE || o == BUAARE) {  // whole polygons: the inside/outside parity needs every edge
        out.segs.insert(out.segs.end(), segs.begin(), segs.end());
      } else {  // reflector lines: only the pieces near the region
        for (const auto& g : segs) {
          GeoBox sb{std::min(g.lon1, g.lon2), std::min(g.lat1, g.lat2), std::max(g.lon1, g.lon2), std::max(g.lat1, g.lat2)};
          if (Overlaps(sb, box)) out.segs.push_back(g);
        }
      }
    } else if (ft.prim == 1) {
      for (const Fsp& s : ft.fs) {
        if (s.rcnm != 110 && s.rcnm != 120) continue;
        auto it = c.vecs.find(Key(s.rcnm, s.rcid));
        if (it == c.vecs.end() || it->second.v.empty()) continue;
        double lon = it->second.v[0].first, lat = it->second.v[0].second;
        if (lon < box.lon_min || lon > box.lon_max || lat < box.lat_min || lat > box.lat_max) continue;
        out.pts.push_back({lon, lat, (uint8_t)PointLevel(o)});
      }
    }
  }
  return true;
}

std::vector<std::string> FindEncCells(const std::vector<std::string>& dirs) {
  std::vector<std::string> cells;
  std::vector<std::string> todo;
  for (std::string d : dirs) {
    size_t m = d.find_first_of("^*");
    if (m != std::string::npos) d.resize(m);
    while (d.size() > 1 && d.back() == '/') d.pop_back();
    if (!d.empty()) todo.push_back(d);
  }
  std::sort(todo.begin(), todo.end());
  todo.erase(std::unique(todo.begin(), todo.end()), todo.end());
  while (!todo.empty()) {
    std::string d = todo.back();
    todo.pop_back();
    DIR* dp = opendir(d.c_str());
    if (!dp) continue;
    while (dirent* e = readdir(dp)) {
      std::string n = e->d_name;
      if (n == "." || n == "..") continue;
      std::string full = d + "/" + n;
      struct stat st;
      if (stat(full.c_str(), &st) != 0) continue;
      if (S_ISDIR(st.st_mode)) todo.push_back(full);
      else if (n.size() > 4 && n.compare(n.size() - 4, 4, ".000") == 0) cells.push_back(full);
    }
    closedir(dp);
  }
  std::sort(cells.begin(), cells.end());
  return cells;
}

void CollectRegion(const std::vector<std::string>& cells, double lat, double lon, double radius_nm,
                   const std::string& index_file, EncData& out) {
  const double dlat = radius_nm / 60.0;
  const double dlon = dlat / std::max(0.1, std::cos(lat * 3.14159265358979 / 180.0));
  const GeoBox box{lon - dlon, lat - dlat, lon + dlon, lat + dlat};

  struct Entry { long mtime; GeoBox ext; };
  std::map<std::string, Entry> index;
  bool index_dirty = false;
  if (!index_file.empty()) {
    std::ifstream in(index_file);
    std::string line;
    while (std::getline(in, line)) {
      std::istringstream ss(line);
      std::string path;
      Entry e;
      if (std::getline(ss, path, '\t') && (ss >> e.mtime >> e.ext.lon_min >> e.ext.lat_min >> e.ext.lon_max >> e.ext.lat_max))
        index[path] = e;
    }
  }
  out.cells_found = (int)cells.size();
  for (const std::string& path : cells) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0 || IsCloudPlaceholder(st)) { out.cells_skipped++; continue; }
    GeoBox ext;
    auto it = index.find(path);
    if (it != index.end() && it->second.mtime == (long)st.st_mtime) {
      ext = it->second.ext;
    } else {
      std::string err;
      if (!S57CellExtent(path, ext, err)) { out.cells_skipped++; continue; }
      index[path] = {(long)st.st_mtime, ext};
      index_dirty = true;
    }
    if (!Overlaps(ext, box)) continue;
    std::string err;
    const int layer = (int)out.layers.size();
    // navigational purpose = third character of the cell name (1 overview ... 6 berthing)
    std::string base = path.substr(path.find_last_of('/') + 1);
    int band = (base.size() > 2 && base[2] >= '0' && base[2] <= '9') ? base[2] - '0' : 0;
    out.layers.push_back({ext.lon_min, ext.lat_min, ext.lon_max, ext.lat_max, band, false});
    out.layers.push_back({ext.lon_min, ext.lat_min, ext.lon_max, ext.lat_max, band, true});  // built-up areas of this cell
    if (ReadS57Cell(path, box, layer, layer + 1, out, err)) out.cells_used++;
    else { out.layers.pop_back(); out.layers.pop_back(); out.cells_skipped++; }
  }
  if (index_dirty && !index_file.empty()) {
    std::string tmp = index_file + ".tmp";
    {
      std::ofstream o(tmp);
      o.precision(9);
      for (auto& kv : index)
        o << kv.first << '\t' << kv.second.mtime << ' ' << kv.second.ext.lon_min << ' ' << kv.second.ext.lat_min << ' '
          << kv.second.ext.lon_max << ' ' << kv.second.ext.lat_max << '\n';
    }
    std::rename(tmp.c_str(), index_file.c_str());
  }
}

}  // namespace radarsim
