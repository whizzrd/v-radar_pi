// Offline PPI with the clutter model, for tuning without OpenCPN.
// usage: ppi_clutter enc.txt lat lon range_m out.bmp gain sea rain wave_m rain_preset [wind_kn]
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "../src/clutter.h"
#include "../src/scene.h"
int main(int argc, char** argv) {
  radarsim::EncScene s; std::string err;
  if (argc < 11 || !s.Load(argv[1], err)) { fprintf(stderr, "usage/load: %s\n", err.c_str()); return 1; }
  radarsim::OwnShip ship; ship.lat = atof(argv[2]); ship.lon = atof(argv[3]);
  double range = atof(argv[4]); const int N = 800, R = 380;
  radarsim::RadarControls c; c.gain_pct = atoi(argv[6]); c.sea_pct = atoi(argv[7]); c.rain_pct = atoi(argv[8]);
  radarsim::WeatherState w; w.wave_m = atof(argv[9]); int preset = atoi(argv[10]);
  w.wind_ms = (argc > 11 ? atof(argv[11]) : 15) * 0.5144;
  w.cells = radarsim::MakeRainPreset(preset, ship.lat, ship.lon, 1);
  w.convective = preset >= 3 ? 0.9 : preset == 2 ? 0.6 : preset == 1 ? 0.2 : 0;
  radarsim::Clutter cl;
  std::vector<unsigned char> img(N * N * 3, 40);
  for (int a = 0; a < 2048; a++) {
    double b = a * 360.0 / 2048; uint8_t spoke[1024];
    uint8_t land[1024];
    s.RenderSpoke(ship, b, range, spoke, 1024, land);
    cl.Apply(ship, b, range, a, w, c, spoke, 1024, land);
    for (int i = 0; i < 1024; i++) {
      int v = spoke[i]; if (v < 1) continue;
      for (double bb = b - 0.09; bb <= b + 0.09; bb += 0.09) {
        double r = (i + 0.5) / 1024 * R;
        int x = N / 2 + (int)(r * sin(bb * M_PI / 180)), y = N / 2 - (int)(r * cos(bb * M_PI / 180));
        if (x < 0 || y < 0 || x >= N || y >= N) continue;
        unsigned char* p = &img[(y * N + x) * 3];
        if (v >= 10) { p[0] = 60; p[1] = 60; p[2] = 255; }          // BGR: red
        else if (v >= 6) { p[0] = 60; p[1] = 220; p[2] = 60; }       // green
        else if (v >= 2) { p[0] = 255; p[1] = 120; p[2] = 40; }      // blue
        else { p[0] = 120; p[1] = 60; p[2] = 40; }
      }
    }
  }
  FILE* f = fopen(argv[5], "wb");
  unsigned char h[54] = {'B','M'}; int sz = 54 + N * N * 3;
  *(int*)(h + 2) = sz; *(int*)(h + 10) = 54; *(int*)(h + 14) = 40; *(int*)(h + 18) = N; *(int*)(h + 22) = -N;
  *(short*)(h + 26) = 1; *(short*)(h + 28) = 24;
  fwrite(h, 1, 54, f); fwrite(img.data(), 1, img.size(), f); fclose(f);
}
