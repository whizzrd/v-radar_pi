#include "navico_sim.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>

namespace radarsim {
namespace {

// Multicast groups used by Navico radars (taken from a real HALO beacon).
const char* kBeaconGroup = "236.6.7.5"; const uint16_t kBeaconPort = 6878;
const char* kDataGroup = "236.6.7.8";   const uint16_t kDataPort = 6678;
const char* kReportGroup = "236.6.7.9"; const uint16_t kReportPort = 6679;
const char* kSendGroup = "236.6.7.10";  const uint16_t kSendPort = 6680;

const int kSpokes = 2048;        // spokes per revolution (angle_raw / 2)
const int kSpokeSamples = 1024;  // samples per spoke, 4 bit each
const int kSpokesPerPacket = 32;
const double kPi = 3.14159265358979323846;

#pragma pack(push, 1)
struct PackedAddr { uint32_t ip; uint16_t port; };
// Same layout as RadarReport_01B2 in radar_pi (NavicoLocate.cpp).
struct Beacon01B2 {
  uint16_t id;
  char serial[16];
  PackedAddr addr0;
  uint8_t u1[12];
  PackedAddr addr1;
  uint8_t u2[4];
  PackedAddr addr2;
  uint8_t u3[10];
  PackedAddr addr3;
  uint8_t u4[4];
  PackedAddr addr4;
  uint8_t u5[10];
  PackedAddr dataA; uint8_t u6[4]; PackedAddr sendA; uint8_t u7[4]; PackedAddr reportA;
  uint8_t u8[10];
  PackedAddr dataB; uint8_t u9[4]; PackedAddr sendB; uint8_t u10[4]; PackedAddr reportB;
  uint8_t u11[10];
  PackedAddr a11; uint8_t u12[4]; PackedAddr a12; uint8_t u13[4]; PackedAddr a13;
  uint8_t u14[10];
  PackedAddr a14; uint8_t u15[4]; PackedAddr a15; uint8_t u16[4]; PackedAddr a16;
};
#pragma pack(pop)

PackedAddr Addr(const char* ip, uint16_t port) {
  PackedAddr a;
  in_addr_t v = inet_addr(ip);
  std::memcpy(&a.ip, &v, 4);
  a.port = htons(port);
  return a;
}

void PutLE32(uint8_t* p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (v >> (8 * i)) & 0xff; }
void PutLE16(uint8_t* p, uint16_t v) { p[0] = v & 0xff; p[1] = v >> 8; }

}  // namespace

NavicoSim::NavicoSim(SimConfig cfg, std::shared_ptr<Scene> scene, ShipFn ship, std::shared_ptr<Weather> weather)
    : cfg_(std::move(cfg)), scene_(std::move(scene)), ship_(std::move(ship)), weather_(std::move(weather)) {}

NavicoSim::~NavicoSim() { Stop(); }

// Multicast-capable, non-loopback IPv4 interface addresses that are up right now.
static std::vector<std::string> CurrentIps(const std::vector<std::string>& wanted) {
  std::vector<std::string> ips;
  ifaddrs* list = nullptr;
  if (getifaddrs(&list) != 0) return ips;
  for (ifaddrs* a = list; a; a = a->ifa_next) {
    if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET) continue;
    if (!(a->ifa_flags & IFF_UP) || (a->ifa_flags & IFF_LOOPBACK) || !(a->ifa_flags & IFF_MULTICAST)) continue;
    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &((sockaddr_in*)a->ifa_addr)->sin_addr, ip, sizeof ip);
    if (!wanted.empty()) {
      bool ok = false;
      for (auto& w : wanted) ok |= (w == ip);
      if (!ok) continue;
    }
    ips.push_back(ip);
  }
  freeifaddrs(list);
  std::sort(ips.begin(), ips.end());
  return ips;
}

void NavicoSim::OpenNics() {
  for (const std::string& ipstr : CurrentIps(cfg_.interfaces)) {
    Nic n; n.ip = ipstr;
    in_addr ifaddr{}; inet_pton(AF_INET, ipstr.c_str(), &ifaddr);
    n.sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (n.sock < 0) continue;
    // TTL 0 keeps the multicast stream on this computer (the Radar plugin still receives it). The spoke stream is
    // several Mbit/s and is sent as large fragmented datagrams: on Wi-Fi that saturates the whole network.
    unsigned char ttl = share_ ? 2 : 0, loop = 1;
    setsockopt(n.sock, IPPROTO_IP, IP_MULTICAST_IF, &ifaddr, sizeof ifaddr);
    setsockopt(n.sock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, 1);
    setsockopt(n.sock, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, 1);
    int big = 1 << 20;
    setsockopt(n.sock, SOL_SOCKET, SO_SNDBUF, &big, sizeof big);
    // command receiver: join the "send" group on this interface
    n.rx = socket(AF_INET, SOCK_DGRAM, 0);
    int one = 1;
    setsockopt(n.rx, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    setsockopt(n.rx, SOL_SOCKET, SO_REUSEPORT, &one, sizeof one);
    sockaddr_in bindaddr{}; bindaddr.sin_family = AF_INET;
    bindaddr.sin_port = htons(kSendPort); bindaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    ip_mreq mreq{}; mreq.imr_multiaddr.s_addr = inet_addr(kSendGroup); mreq.imr_interface = ifaddr;
    if (bind(n.rx, (sockaddr*)&bindaddr, sizeof bindaddr) != 0 ||
        setsockopt(n.rx, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof mreq) != 0) {
      close(n.rx); n.rx = -1;
    }
    nics_.push_back(n);
  }
  nic_count_ = (int)nics_.size();
}

void NavicoSim::CloseNics() {
  for (auto& n : nics_) { if (n.sock >= 0) close(n.sock); if (n.rx >= 0) close(n.rx); }
  nics_.clear();
  nic_count_ = 0;
}

bool NavicoSim::Start(std::string& error) {
  (void)error;
  // No interface is not fatal: the simulator keeps looking, so it comes alive when a network appears
  // (and follows the Mac when it switches from one network to another).
  share_ = cfg_.share_on_network;
  OpenNics();
  stop_ = false;
  transmit_ = cfg_.start_transmitting;
  thread_ = std::thread([this] { Run(); });
  return true;
}

void NavicoSim::Stop() {
  stop_ = true;
  if (thread_.joinable()) thread_.join();
  CloseNics();
}

void NavicoSim::SendAll(const std::string& group, uint16_t port, const void* data, size_t len) {
  sockaddr_in to{}; to.sin_family = AF_INET; to.sin_port = htons(port); to.sin_addr.s_addr = inet_addr(group.c_str());
  for (auto& n : nics_) sendto(n.sock, data, len, 0, (sockaddr*)&to, sizeof to);
}

void NavicoSim::SendBeacon() {
  for (auto& n : nics_) {
    Beacon01B2 b; std::memset(&b, 0, sizeof b);
    b.id = 0xB201;  // bytes 01 B2
    std::strncpy(b.serial, cfg_.serial.c_str(), sizeof b.serial - 1);
    b.addr0 = Addr(n.ip.c_str(), 0);
    b.dataA = Addr(kDataGroup, kDataPort);
    b.sendA = Addr(kSendGroup, kSendPort);
    b.reportA = Addr(kReportGroup, kReportPort);
    // secondary (dual range) addresses: not simulated
    b.dataB = Addr("236.6.7.13", 6657); b.sendB = Addr("236.6.7.14", 6658); b.reportB = Addr("236.6.7.15", 6659);
    sockaddr_in to{}; to.sin_family = AF_INET; to.sin_port = htons(kBeaconPort); to.sin_addr.s_addr = inet_addr(kBeaconGroup);
    sendto(n.sock, &b, sizeof b, 0, (sockaddr*)&to, sizeof to);
  }
}

// radar_pi sends a control value N (0..100) as (N+1)*255/100 and reads a reported raw value R back as R*100/255;
// these must be inverted exactly, or a +1/-1 step is lost to rounding and the control seems stuck.
static int PctFromCommand(int v) { return std::min(100, std::max(0, (v * 100 + 254) / 255 - 1)); }
static int RawFromPct(int pct) { return std::min(255, (pct * 255 + 99) / 100); }

void NavicoSim::SendReports() {
  uint8_t r01[18] = {0x01, 0xC4, (uint8_t)(transmit_ ? 0x02 : 0x01)};
  SendAll(kReportGroup, kReportPort, r01, sizeof r01);

  uint8_t r02[99] = {0x02, 0xC4};
  PutLE32(r02 + 2, (uint32_t)range_m_ * 10);
  r02[12] = (uint8_t)RawFromPct(gain_);
  r02[13] = (uint8_t)sea_mode_;
  PutLE32(r02 + 8, gain_auto_ ? 1 : 0);
  PutLE32(r02 + 17, (uint32_t)RawFromPct(sea_));
  r02[22] = (uint8_t)RawFromPct(rain_);
  r02[7] = (uint8_t)mode_;
  r02[34] = (uint8_t)interference_;
  r02[38] = (uint8_t)target_exp_;
  r02[42] = (uint8_t)target_boost_;
  SendAll(kReportGroup, kReportPort, r02, sizeof r02);

  uint8_t r03[129] = {0x03, 0xC4, 0x00};
  PutLE32(r03 + 30, 14);  // NST_HALO
  SendAll(kReportGroup, kReportPort, r03, sizeof r03);

  uint8_t r04[66] = {0x04, 0xC4};
  SendAll(kReportGroup, kReportPort, r04, sizeof r04);

  // HALO keeps sea state and the sea clutter value in the 08 C4 report (bytes 2, 14, 15): if these stay
  // zero the Radar plugin resets its Sea control to 0 every second.
  uint8_t r08[21] = {0x08, 0xC4};
  r08[2] = (uint8_t)sea_state_;
  r08[3] = (uint8_t)local_if_;
  r08[4] = (uint8_t)scan_speed_;
  r08[5] = sls_auto_ ? 1 : 0;
  r08[9] = (uint8_t)(sls_ * 255 / 100);
  r08[12] = (uint8_t)noise_rej_;
  r08[13] = (uint8_t)target_sep_;
  r08[14] = (uint8_t)sea_;
  r08[15] = (uint8_t)(int8_t)sea_auto_offset_;
  SendAll(kReportGroup, kReportPort, r08, sizeof r08);
}

void NavicoSim::SendSpokes(int count) {
  static uint8_t pkt[8 + kSpokesPerPacket * (24 + kSpokeSamples / 2)];
  OwnShip ship = ship_();
  const double range = range_m_;
  std::shared_ptr<const WeatherState> weather = weather_ ? weather_->Get() : nullptr;
  int in_packet = 0;
  for (int i = 0; i < count; i++) {
    if (in_packet == 0) std::memset(pkt, 0, 8);  // frame header: zeros
    uint8_t* line = pkt + 8 + in_packet * (24 + kSpokeSamples / 2);
    std::memset(line, 0, 24);
    line[0] = 0x18; line[1] = 0x02;
    PutLE16(line + 2, scan_ & 0x0fff);
    line[4] = 0x00; line[5] = 0x44;
    // HALO range: large==0x80 -> small/4 metres, else large*small/512 metres
    if (range * 4 < 65535) { PutLE16(line + 6, 0x80); PutLE16(line + 12, (uint16_t)(range * 4)); }
    else { PutLE16(line + 6, (uint16_t)range); PutLE16(line + 12, 512); }
    PutLE16(line + 8, (uint16_t)(next_spoke_ * 2));  // angle, 4096 per revolution
    PutLE16(line + 10, 0x8000);                       // heading: not provided
    PutLE16(line + 14, 0);
    std::memset(line + 16, 0xff, 8);

    uint8_t samples[kSpokeSamples];
    double bearing = ship.heading_deg + next_spoke_ * 360.0 / kSpokes;
    uint8_t land[kSpokeSamples];
    scene_->RenderSpoke(ship, std::fmod(bearing + 720.0, 360.0), range, samples, kSpokeSamples, land);
    if (weather) {
      RadarControls rc;
      rc.gain_pct = gain_; rc.gain_auto = gain_auto_; rc.sea_pct = sea_; rc.sea_mode = sea_mode_; rc.rain_pct = rain_;
      rc.noise_rejection = noise_rej_; rc.target_expansion = target_exp_; rc.target_boost = target_boost_;
      rc.target_separation = target_sep_; rc.interference_rejection = interference_; rc.local_interference_rejection = local_if_;
      clutter_.Apply(ship, std::fmod(bearing + 720.0, 360.0), range, scan_, *weather, rc, samples, kSpokeSamples, land);
    }
    for (int k = 0; k < kSpokeSamples / 2; k++)
      line[24 + k] = (samples[2 * k] & 0x0f) | ((samples[2 * k + 1] & 0x0f) << 4);

    next_spoke_ = (next_spoke_ + 1) % kSpokes;
    scan_++;
    spokes_sent_++;
    if (++in_packet == kSpokesPerPacket) {
      SendAll(kDataGroup, kDataPort, pkt, sizeof pkt);
      in_packet = 0;
    }
  }
  // 'count' is always a multiple of 32 (see Run)
}

// A HALO changes a group of settings by itself when a use mode is selected, and reports them back.
// Custom leaves everything as it is. The presets are plausible choices for the intended use.
void NavicoSim::ApplyMode(int mode) {
  struct Preset { bool gain_auto; int gain; int sea_mode; int sea; int rain; int noise; int expansion; int boost; int sep; };
  static const Preset presets[] = {
      {false, 0, 0, 0, 0, 0, 0, 0, 0},    // 0 custom: unused
      {true, 50, 1, 0, 0, 1, 0, 0, 2},    // 1 harbor: auto gain, harbour sea clutter, crowded targets kept apart
      {true, 50, 2, 0, 0, 1, 0, 0, 0},    // 2 offshore: auto gain, offshore sea clutter
      {false, 65, 2, 0, 0, 0, 1, 2, 0},   // 3 buoy: sensitive to small, weak targets
      {true, 50, 0, 0, 0, 2, 0, 0, 0},    // 4 weather: no rain or sea suppression, so precipitation shows
      {false, 70, 2, 30, 0, 0, 2, 2, 0},  // 5 bird: high sensitivity, strong target enhancement
  };
  if (mode <= 0 || mode >= (int)(sizeof presets / sizeof presets[0])) return;
  const Preset& s = presets[mode];
  gain_auto_ = s.gain_auto; if (!s.gain_auto) gain_ = s.gain;
  sea_mode_ = s.sea_mode; sea_ = s.sea; rain_ = s.rain;
  noise_rej_ = s.noise; target_exp_ = s.expansion; target_boost_ = s.boost; target_sep_ = s.sep;
}

void NavicoSim::HandleCommand(const uint8_t* p, size_t len) {
  if (len < 3) return;
  static FILE* dbg = std::getenv("VRADAR_DEBUG") ? std::fopen((std::string(std::getenv("HOME")) + "/Library/Logs/v-radar-commands.log").c_str(), "a") : nullptr;
  if (dbg) {
    std::fprintf(dbg, "cmd");
    for (size_t i = 0; i < len && i < 24; i++) std::fprintf(dbg, " %02x", p[i]);
    std::fprintf(dbg, "  (before: gain=%d sea=%d mode=%d rain=%d)\n", (int)gain_, (int)sea_, (int)sea_mode_, (int)rain_);
    std::fflush(dbg);
  }
  if (p[1] == 0xC1) {
    switch (p[0]) {
      case 0x00: case 0x01:  // transmit on/off (sent as two packets)
        if (len == 3) transmit_ = (p[2] == 0x01);
        SendReports();
        break;
      case 0x03:
        if (len >= 6) {
          uint32_t dm = p[2] | (p[3] << 8) | (p[4] << 16) | ((uint32_t)p[5] << 24);
          if (dm >= 500 && dm <= 727040) range_m_ = dm / 10;
          SendReports();
        }
        break;
      case 0x06:  // gain (p[2]=0), rain (p[2]=4); older radars also use it for sea (p[2]=2)
        if (len >= 11) {
          int pct = PctFromCommand((int)p[10]);
          if (p[2] == 0x00) { gain_ = pct; gain_auto_ = p[6] != 0; }
          else if (p[2] == 0x02) { sea_ = pct; sea_mode_ = p[6] ? 2 : 0; }
          else if (p[2] == 0x04) { rain_ = pct; }
          else if (p[2] == 0x05) { sls_ = std::min(100, (int)p[10] * 100 / 256); sls_auto_ = p[6] != 0; }
          SendReports();
        }
        break;
      case 0x11:  // HALO sea clutter: p[5]=1 mode, 2 manual value, 4 auto offset
        if (len >= 6) {
          if (p[5] == 0x01) sea_mode_ = p[2] ? 2 : 0;
          else if (p[5] == 0x02) sea_ = std::min(100, (int)p[4]);
          else if (p[5] == 0x04) sea_auto_offset_ = (int)(int8_t)p[4];
          SendReports();
        }
        break;
      case 0x09: case 0x12:  // target expansion (0x12 on HALO)
        target_exp_ = std::min(3, (int)p[2]); SendReports(); break;
      case 0x0a:  // target boost
        target_boost_ = std::min(2, (int)p[2]); SendReports(); break;
      case 0x0e:  // local interference rejection
        local_if_ = std::min(3, (int)p[2]); SendReports(); break;
      case 0x0f:  // scan speed
        scan_speed_ = p[2]; SendReports(); break;
      case 0x10:  // mode (use mode: 0 custom, 1 harbor, 2 offshore, 3 buoy, 4 weather, 5 bird)
        mode_ = p[2]; ApplyMode(p[2]); SendReports(); break;
      case 0x21:  // noise rejection
        noise_rej_ = std::min(3, (int)p[2]); SendReports(); break;
      case 0x22:  // target separation
        target_sep_ = std::min(3, (int)p[2]); SendReports(); break;
      case 0x0b:  // sea state
        sea_state_ = p[2];
        SendReports();
        break;
      case 0x08:  // interference rejection
        interference_ = p[2];
        SendReports();
        break;
      default: break;
    }
  }
}

void NavicoSim::Run() {
  using clk = std::chrono::steady_clock;
  auto last_beacon = clk::now() - std::chrono::seconds(10);
  auto last_report = last_beacon;
  auto last_spokes = clk::now();
  auto last_nic_check = clk::now();
  double owed = 0;
  while (!stop_) {
    // commands
    fd_set fds; FD_ZERO(&fds); int maxfd = -1;
    for (auto& n : nics_) if (n.rx >= 0) { FD_SET(n.rx, &fds); maxfd = std::max(maxfd, n.rx); }
    timeval tv{0, 5000};
    if (maxfd >= 0 && select(maxfd + 1, &fds, nullptr, nullptr, &tv) > 0) {
      for (auto& n : nics_) {
        if (n.rx >= 0 && FD_ISSET(n.rx, &fds)) {
          uint8_t buf[1500];
          ssize_t r = recv(n.rx, buf, sizeof buf, 0);
          if (r > 0) HandleCommand(buf, (size_t)r);
        }
      }
    } else if (maxfd < 0) { usleep(5000); }

    auto now = clk::now();
    if (now - last_nic_check > std::chrono::seconds(3)) {
      last_nic_check = now;
      std::vector<std::string> have;
      for (auto& n : nics_) have.push_back(n.ip);
      std::sort(have.begin(), have.end());
      if (reopen_.exchange(false) || have != CurrentIps(cfg_.interfaces)) { CloseNics(); OpenNics(); last_beacon = now - std::chrono::seconds(10); }
    }
    if (now - last_beacon > std::chrono::seconds(2)) { SendBeacon(); last_beacon = now; }
    if (now - last_report > std::chrono::seconds(1)) { SendReports(); last_report = now; }
    if (transmit_) {
      owed += std::chrono::duration<double>(now - last_spokes).count() * cfg_.spokes_per_second * (scan_speed_ >= 1 ? 1.5 : 1.0);  // fast scan: 36 instead of 24 rpm
      int packets = (int)(owed / kSpokesPerPacket);
      if (packets > 8) packets = 8;  // never burst after a stall
      if (packets > 0) { SendSpokes(packets * kSpokesPerPacket); owed -= packets * kSpokesPerPacket; }
    } else owed = 0;
    last_spokes = now;
  }
}

}  // namespace radarsim
