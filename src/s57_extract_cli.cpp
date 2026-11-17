// Dev tool: writes the same text format as tools/enc_extract.py (used to validate the native reader).
// usage: s57_extract_cli LAT LON RADIUS_NM OUT ENC_DIR [ENC_DIR...]
#include <chrono>
#include <cstdio>
#include <cstdlib>

#include "s57_reader.h"

int main(int argc, char** argv) {
  if (argc < 6) { fprintf(stderr, "usage: %s LAT LON RADIUS_NM OUT ENC_DIR...\n", argv[0]); return 1; }
  std::vector<std::string> dirs(argv + 5, argv + argc);
  auto t0 = std::chrono::steady_clock::now();
  auto cells = radarsim::FindEncCells(dirs);
  radarsim::EncData d;
  radarsim::CollectRegion(cells, atof(argv[1]), atof(argv[2]), atof(argv[3]), "", d);
  FILE* f = fopen(argv[4], "w");
  for (auto& s : d.segs) fprintf(f, "%c %.6f %.6f %.6f %.6f%s\n", s.kind == 2 ? 'E' : 'L', s.lon1, s.lat1, s.lon2, s.lat2, s.kind == 1 ? " X" : "");
  for (auto& p : d.pts) fprintf(f, "P %.6f %.6f %d\n", p.lon, p.lat, p.level);
  fclose(f);
  double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  fprintf(stderr, "cells found %d used %d skipped %d; %zu segs %zu pts; %.2fs\n", d.cells_found, d.cells_used, d.cells_skipped, d.segs.size(), d.pts.size(), dt);
}
