// Standalone harness: runs the simulated radar without OpenCPN's plugin loader.
// usage: navico_sim_cli [lat lon heading_deg [enc_extract.txt [ais_udp_port]]]
// With an ENC extract the radar sees land and marks; with an AIS port, simulated vessels are
// drawn as radar echoes and sent as AIVDM over UDP to 127.0.0.1:<port>.
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <thread>

#include "ais_sim.h"
#include "navico_sim.h"

static volatile std::sig_atomic_t g_stop = 0;

static void SendLines(int sock, const sockaddr_in& to, const std::string& blob) {
  std::istringstream ss(blob);
  std::string line;
  while (std::getline(ss, line))
    if (!line.empty()) { line += "\r\n"; sendto(sock, line.data(), line.size(), 0, (const sockaddr*)&to, sizeof to); }
}

int main(int argc, char** argv) {
  radarsim::OwnShip ship;
  ship.lat = argc > 1 ? atof(argv[1]) : 52.6396;
  ship.lon = argc > 2 ? atof(argv[2]) : 5.0670;
  ship.heading_deg = argc > 3 ? atof(argv[3]) : 0.0;
  const int ais_port = argc > 5 ? atoi(argv[5]) : 0;
  std::signal(SIGINT, [](int) { g_stop = 1; });
  std::signal(SIGTERM, [](int) { g_stop = 1; });
  radarsim::SimConfig cfg; cfg.start_transmitting = true;
  std::string err;
  std::shared_ptr<radarsim::Scene> scene = std::make_shared<radarsim::TestScene>();
  std::shared_ptr<radarsim::EncScene> enc;
  if (argc > 4) {
    enc = std::make_shared<radarsim::EncScene>();
    if (!enc->Load(argv[4], err)) { fprintf(stderr, "ENC load failed: %s\n", err.c_str()); return 1; }
    scene = enc;
  }
  radarsim::NavicoSim sim(cfg, scene, [ship] { return ship; });
  if (!sim.Start(err)) { fprintf(stderr, "start failed: %s\n", err.c_str()); return 1; }
  fprintf(stderr, "simulated HALO running at %.4f %.4f hdg %.0f\n", ship.lat, ship.lon, ship.heading_deg);

  radarsim::AisSim ais(ship.lat, ship.lon, [&](double la, double lo) { return scene->IsWater(la, lo); }, 20);
  int sock = socket(AF_INET, SOCK_DGRAM, 0);
  sockaddr_in to{}; to.sin_family = AF_INET; to.sin_port = htons(ais_port); to.sin_addr.s_addr = inet_addr("127.0.0.1");
  int tick = 0;
  while (!g_stop) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    ais.Tick(1.0 * 20);  // demo time runs 20x so vessels visibly move
    if (enc) enc->SetTargets(ais.Targets());
    if (ais_port) {
      for (auto& s : ais.PositionSentences()) SendLines(sock, to, s);
      if (tick % 10 == 0) for (auto& s : ais.StaticSentences()) SendLines(sock, to, s);
    }
    if (++tick % 5 == 0)
      fprintf(stderr, "tx=%d range=%dm spokes=%llu\n", (int)sim.transmitting(), sim.range_m(), (unsigned long long)sim.spokes_sent());
  }
  return 0;
}
