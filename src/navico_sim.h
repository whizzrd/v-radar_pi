#pragma once
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "clutter.h"
#include "scene.h"
#include "weather.h"

namespace radarsim {

struct SimConfig {
  std::string serial = "1930201234";  // starts with "19": radar_pi treats it as HALO
  std::vector<std::string> interfaces;  // IPv4 addresses; empty = all multicast-capable non-loopback
  bool start_transmitting = false;      // demo convenience: begin in transmit mode
  bool share_on_network = false;        // false: multicast TTL 0, the radar stays on this computer
  int spokes_per_second = 819;          // 24 rpm x 2048 spokes
};

// Simulates one Navico HALO radar: beacon, status reports, spoke data; honours the
// basic commands (transmit on/off, range, gain).
class NavicoSim {
 public:
  using ShipFn = std::function<OwnShip()>;
  NavicoSim(SimConfig cfg, std::shared_ptr<Scene> scene, ShipFn ship, std::shared_ptr<Weather> weather = nullptr);
  ~NavicoSim();
  bool Start(std::string& error);
  void Stop();
  bool transmitting() const { return transmit_; }
  int range_m() const { return range_m_; }
  uint64_t spokes_sent() const { return spokes_sent_; }
  // Switch between host-only (TTL 0) and network-wide multicast; takes effect within a few seconds.
  void SetShareOnNetwork(bool share) { if (share_ != share) { share_ = share; reopen_ = true; } }
  bool sharing() const { return share_; }
  int network_interfaces() const { return nic_count_; }  // interfaces currently in use (0 = no network)

 private:
  struct Nic { std::string ip; int sock = -1; int rx = -1; };
  void Run();
  void OpenNics();
  void CloseNics();
  void SendAll(const std::string& group, uint16_t port, const void* data, size_t len);
  void SendBeacon();
  void SendReports();
  void SendSpokes(int count);
  void HandleCommand(const uint8_t* p, size_t len);
  void ApplyMode(int mode);

  SimConfig cfg_;
  std::shared_ptr<Scene> scene_;
  ShipFn ship_;
  std::vector<Nic> nics_;
  std::thread thread_;
  std::atomic<bool> stop_{false};
  std::atomic<int> nic_count_{0};
  std::atomic<bool> share_{false};
  std::atomic<bool> reopen_{false};
  std::atomic<bool> transmit_{false};
  std::atomic<int> range_m_{1852};
  std::atomic<int> gain_{50};        // percent
  std::atomic<bool> gain_auto_{false};
  std::atomic<int> sea_{0};          // percent
  std::atomic<int> sea_mode_{0};     // 0 manual, 1 harbour, 2 offshore
  std::atomic<int> rain_{0};         // percent
  std::atomic<int> interference_{0};
  std::atomic<int> noise_rej_{0}, target_exp_{0}, target_boost_{0}, target_sep_{0}, local_if_{0};
  std::atomic<int> scan_speed_{0};   // 0 = 24 rpm, >= 1 = fast scan (36 rpm)
  std::atomic<int> mode_{0};         // HALO use mode (0 custom, 1 harbor, 2 offshore, 3 buoy, 4 weather, 5 bird)
  std::atomic<int> sls_{0};          // side lobe suppression percent (reported only)
  std::atomic<bool> sls_auto_{true};
  std::atomic<int> sea_state_{0};    // HALO sea state: 0 calm, 1 moderate, 2 rough
  std::atomic<int> sea_auto_offset_{0};
  std::shared_ptr<Weather> weather_;
  Clutter clutter_;
  std::atomic<uint64_t> spokes_sent_{0};
  int next_spoke_ = 0;
  uint16_t scan_ = 0;
};

}  // namespace radarsim
