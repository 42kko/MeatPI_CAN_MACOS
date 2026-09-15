#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
namespace meatcan {
constexpr uint32_t no_echo = 0xffffffffu;
inline uint32_t get32(const uint8_t *p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) |
         (uint32_t(p[3]) << 24);
}
inline void put32(uint8_t *p, uint32_t x) {
  for (int i = 0; i < 4; i++)
    p[i] = uint8_t(x >> (8 * i));
}
struct Frame {
  uint32_t echo = no_echo, id = 0;
  uint8_t dlc = 0, flags = 0;
  std::array<uint8_t, 8> data{};
};
inline uint32_t bitrate(std::string s) {
  if (!std::regex_match(s, std::regex("[0-9]+[kKmM]?")))
    throw std::runtime_error("invalid bitrate");
  uint64_t m = 1;
  if (s.back() == 'k' || s.back() == 'K') {
    m = 1000;
    s.pop_back();
  } else if (s.back() == 'm' || s.back() == 'M') {
    m = 1000000;
    s.pop_back();
  }
  auto raw = std::stoull(s);
  if (raw > 1000000 / m)
    throw std::runtime_error("bitrate must be 1..1000000");
  auto n = raw * m;
  if (n < 1 || n > 1000000)
    throw std::runtime_error("bitrate must be 1..1000000");
  return uint32_t(n);
}
inline Frame parse(std::string s) {
  std::smatch m;
  if (!std::regex_match(s, m,
                        std::regex("([0-9a-fA-F]{1,8})#([0-9a-fA-F]{0,16})")) ||
      m[2].length() % 2)
    throw std::runtime_error("expected Classic CAN ID#HEX, e.g. 123#01020304");
  Frame f;
  f.id = uint32_t(std::stoul(m[1], nullptr, 16));
  bool ext = m[1].length() > 3;
  if (f.id > (ext ? 0x1fffffffu : 0x7ffu))
    throw std::runtime_error("CAN ID out of range");
  if (ext)
    f.id |= 0x80000000u;
  std::string d = m[2];
  f.dlc = d.size() / 2;
  for (size_t i = 0; i < f.dlc; i++)
    f.data[i] = uint8_t(std::stoul(d.substr(i * 2, 2), nullptr, 16));
  return f;
}
inline std::string format(const Frame &f) {
  std::ostringstream o;
  o << std::uppercase << std::hex << std::setfill('0')
    << std::setw((f.id & 0x80000000u) ? 8 : 3) << (f.id & 0x1fffffffu) << '#';
  if (f.id & 0x40000000u) {
    o << "R" << unsigned(f.dlc);
    return o.str();
  }
  for (unsigned i = 0; i < f.dlc; i++)
    o << std::setw(2) << unsigned(f.data[i]);
  return o.str();
}
inline std::vector<uint8_t> encode(const Frame &f) {
  std::vector<uint8_t> b(20);
  put32(b.data(), f.echo);
  put32(b.data() + 4, f.id);
  b[8] = f.dlc;
  std::copy(f.data.begin(), f.data.end(), b.begin() + 12);
  return b;
}
// Timestamp mode is explicitly disabled. Accept the negotiated 20-byte layout
// only. Packet capacity is NOT frame length. No guessing or silently discarding
// trailing bytes.
inline std::vector<Frame> decode(const uint8_t *b, size_t n,
                                 bool timestamps = false) {
  size_t stride = timestamps ? 24 : 20;
  if (n % stride)
    throw std::runtime_error("unexpected GS USB transfer length " +
                             std::to_string(n) + " (expected multiple of " +
                             std::to_string(stride) + ")");
  std::vector<Frame> out;
  for (size_t at = 0; at < n; at += stride) {
    auto p = b + at;
    Frame f;
    f.echo = get32(p);
    f.id = get32(p + 4);
    f.dlc = p[8];
    f.flags = p[10];
    if (f.dlc > 8 || p[9] != 0 || (f.flags & ~1u) || p[11])
      throw std::runtime_error("unsupported or malformed GS USB frame");
    std::copy(p + 12, p + 20, f.data.begin());
    out.push_back(f);
  }
  return out;
}
struct Timing {
  uint32_t brp = 0, tseg1 = 0, tseg2 = 0;
};
inline Timing timing(const std::array<uint32_t, 10> &c, uint32_t rate) {
  if (!rate || !c[1] || !c[7] || !c[9] || c[8] > 65536 || c[3] > 256 ||
      c[5] > 256 || c[2] < 1 || c[4] < 1 || c[6] < 1)
    throw std::runtime_error("invalid timing capabilities");
  Timing best;
  double score = 1e9;
  for (uint32_t b = c[7]; b <= c[8]; b += c[9])
    for (uint32_t a = std::max(2u, c[2]); a <= c[3]; a++)
      for (uint32_t z = c[4]; z <= c[5]; z++) {
        auto n = 1 + a + z;
        if (uint64_t(rate) * b * n != c[1])
          continue;
        double x = std::abs(double(1 + a) / n - .875);
        if (x < score - 1e-12 ||
            (std::abs(x - score) < 1e-12 && n > 1 + best.tseg1 + best.tseg2)) {
          best = {b, a, z};
          score = x;
        }
      }
  if (!best.brp)
    throw std::runtime_error(
        "requested bitrate cannot be produced exactly by device clock");
  return best;
}
} // namespace meatcan
