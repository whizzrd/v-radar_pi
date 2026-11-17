#pragma once
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace radarsim {

// Radar-relevant features of S-57 ENC cells, in lon/lat degrees.
struct EncData {
  // kind: 0 land polygon edge, 1 land polygon edge on the cell border (parity only), 2 line reflector
  // layer: index into `layers` (the cell the edge came from). Land is filled per layer and the layers are
  // united, so cells of different scale bands that overlap do not cancel each other out.
  struct Seg { double lon1, lat1, lon2, lat2; uint8_t kind; uint16_t layer; };
  struct Pt { double lon, lat; uint8_t level; };  // level: echo strength 0..15
  struct Layer { double lon_min, lat_min, lon_max, lat_max; int band; bool urban = false; };  // extent of the cell; navigational band 1..6; urban = built-up areas of that cell
  std::vector<Seg> segs;
  std::vector<Pt> pts;
  std::vector<Layer> layers;
  int cells_used = 0;     // cells that overlapped the region
  int cells_found = 0;    // cells seen in the search directories
  int cells_skipped = 0;  // cloud placeholders (not downloaded) or unreadable files
};

struct GeoBox { double lon_min, lat_min, lon_max, lat_max; };

// All *.000 base cells below the given directories (OpenCPN chart directories). A trailing
// '^' or '*' marker, as OpenCPN stores in its chart directory list, is ignored.
// cancel (optional): when it becomes true the search stops early and returns what it has.
std::vector<std::string> FindEncCells(const std::vector<std::string>& dirs, const std::atomic<bool>* cancel = nullptr);

// Reads every cell that overlaps a radius around (lat, lon). index_file (may be empty) caches the
// cell extents so that later calls only open the cells that matter. Cells that are cloud
// placeholders (macOS "dataless") are skipped: reading them would trigger a download.
// cancel (optional): checked between cells; the extents found so far are still saved to index_file.
void CollectRegion(const std::vector<std::string>& cells, double lat, double lon, double radius_nm,
                   const std::string& index_file, EncData& out, const std::atomic<bool>* cancel = nullptr);

// Parses one cell, keeping features that touch box. Returns false (with err) on a malformed file.
// Update files (.001 ...) are not applied.
bool ReadS57Cell(const std::string& path, const GeoBox& box, int layer, int urban_layer, EncData& out, std::string& err);

// Extent of all vector data of a cell (cheap: no feature assembly).
bool S57CellExtent(const std::string& path, GeoBox& box, std::string& err);

}  // namespace radarsim
