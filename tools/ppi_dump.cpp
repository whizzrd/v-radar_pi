// Renders the ENC scene as a PPI image (BMP) without any networking, for debugging.
// usage: ppi_dump enc.txt lat lon range_m out.bmp
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "../src/scene.h"
int main(int argc, char** argv) {
  radarsim::EncScene s; std::string err;
  if (argc < 6 || !s.Load(argv[1], err)) { fprintf(stderr, "load: %s\n", err.c_str()); return 1; }
  radarsim::OwnShip ship; ship.lat = atof(argv[2]); ship.lon = atof(argv[3]);
  double range = atof(argv[4]); const int N = 800, R = 380;
  std::vector<unsigned char> img(N * N * 3, 20);
  for (int a = 0; a < 3600; a++) {
    double b = a * 0.1; uint8_t spoke[1024];
    s.RenderSpoke(ship, b, range, spoke, 1024);
    for (int i = 0; i < 1024; i++) {
      if (!spoke[i]) continue;
      double r = (i + 0.5) / 1024 * R;
      int x = N / 2 + (int)(r * sin(b * M_PI / 180)), y = N / 2 - (int)(r * cos(b * M_PI / 180));
      if (x < 0 || y < 0 || x >= N || y >= N) continue;
      unsigned char* p = &img[(y * N + x) * 3];
      int v = spoke[i];
      p[0] = v > 8 ? 255 : 30; p[1] = v > 8 ? 60 : (v > 2 ? 160 : 90); p[2] = v > 8 ? 60 : 255;  // BGR later
    }
  }
  FILE* f = fopen(argv[5], "wb");
  unsigned char h[54] = {'B','M'}; int sz = 54 + N * N * 3;
  *(int*)(h + 2) = sz; *(int*)(h + 10) = 54; *(int*)(h + 14) = 40; *(int*)(h + 18) = N; *(int*)(h + 22) = -N;
  *(short*)(h + 26) = 1; *(short*)(h + 28) = 24;
  fwrite(h, 1, 54, f); fwrite(img.data(), 1, img.size(), f); fclose(f);
}
